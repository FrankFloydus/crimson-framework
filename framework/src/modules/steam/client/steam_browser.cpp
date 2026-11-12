// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_browser.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_html_callback.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/time.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Bound policy work and native waiting time per open request.
constexpr int64_t maximum_origins = 16;
constexpr int64_t maximum_url_bytes = 2048;
constexpr uint64_t browser_open_timeout_msec = 15000;

// Return an exact native client owner only while its original scope is live.
std::shared_ptr<SteamRuntime> live_runtime(const std::weak_ptr<SteamRuntime> &runtime,
        const std::weak_ptr<SteamClientScope> &scope) {
    if (SteamRuntime::on_main_thread()) {
        const auto owner = scope.lock();
        if (owner && owner->active) {
            return runtime.lock();
        }
    }
    return {};
}

// Validate a DNS name as ASCII labels without characters that Chromium could reinterpret as authority syntax.
bool valid_host(const String &host) {
    if (host.is_empty() || host.length() > 253) {
        return false;
    }
    const PackedStringArray labels = host.split(".");
    for (int64_t index = 0; index < labels.size(); ++index) {
        const String &label = labels[index];
        if (label.is_empty() || label.length() > 63 || label.begins_with("-") || label.ends_with("-")) {
            return false;
        }
        for (int64_t offset = 0; offset < label.length(); ++offset) {
            const char32_t character = label[offset];
            if (!((character >= U'a' && character <= U'z') || (character >= U'0' && character <= U'9') ||
                        character == U'-')) {
                return false;
            }
        }
    }
    return true;
}

// Canonicalize supported HTTPS and explicitly allowlisted loopback HTTP origins.
String read_origin(const String &url, bool origin_only = false) {
    if (url.is_empty() || url.length() > maximum_url_bytes) {
        return {};
    }
    const String prefix = url.substr(0, 8).to_lower();
    const bool https = prefix.begins_with("https://");
    const int64_t start = https ? 8 : prefix.begins_with("http://") ? 7 : 0;
    if (start == 0 || start >= url.length()) {
        return {};
    }
    int64_t end = url.length();
    for (int64_t index = start; index < url.length(); ++index) {
        const char32_t character = url[index];
        if (character <= U' ' || character >= U'\x7f' || character == U'\\') {
            return {};
        }
        if (end == url.length() && (character == U'/' || character == U'?' || character == U'#')) {
            end = index;
        }
    }
    if (origin_only && end < url.length() && url.substr(end) != "/") {
        return {};
    }
    const String authority = url.substr(start, end - start).to_lower();
    if (authority.is_empty() || authority.contains("@")) {
        return {};
    }
    int64_t port_start = authority.find(":");
    String host;
    if (authority.begins_with("[")) {
        const int64_t close = authority.find("]");
        const String ip = close > 1 ? authority.substr(1, close - 1) : String();
        if (!ip.contains(":") || !ip.is_valid_ip_address()) {
            return {};
        }
        host = authority.substr(0, close + 1);
        port_start = close + 1 < authority.length() ? close + 1 : -1;
    } else {
        host = port_start == -1 ? authority : authority.substr(0, port_start);
        if (!valid_host(host)) {
            return {};
        }
    }
    if (!https && host != "127.0.0.1" && host != "[::1]") {
        return {};
    }
    const int64_t default_port = https ? 443 : 80;
    int64_t port = default_port;
    if (port_start != -1) {
        const String suffix = authority.substr(port_start + 1);
        if (authority[port_start] != U':' || suffix.is_empty() || suffix.length() > 5 ||
                !suffix.is_valid_int() || suffix.begins_with("+")) {
            return {};
        }
        port = suffix.to_int();
        if (port <= 0 || port > 65535) {
            return {};
        }
    }
    const String origin = String(https ? "https://" : "http://") + host;
    return port == default_port ? origin : origin + String(":") + String::num_int64(port);
}

// Parse policy origins and add the HTTPS initial origin automatically.
bool resolve_policy(const String &url, const PackedStringArray &extra, PackedStringArray &out) {
    const String initial = read_origin(url);
    if (initial.is_empty() || extra.size() > maximum_origins) {
        return false;
    }
    for (int64_t index = 0; index < extra.size(); ++index) {
        const String origin = read_origin(extra[index], true);
        if (origin.is_empty()) {
            return false;
        }
        if (!out.has(origin)) {
            out.append(origin);
        }
    }
    if (initial.begins_with("http://")) {
        return out.has(initial);
    }
    if (!out.has(initial)) {
        out.append(initial);
    }
    return true;
}

// Compare a request's exact canonical origin against the fixed policy.
bool permitted(const PackedStringArray &origins, const String &url) {
    const String origin = read_origin(url);
    return !origin.is_empty() && origins.has(origin);
}

// Ignore fragment-only changes when deciding if a new page serial must arrive.
String without_fragment(const String &url) {
    const int64_t index = url.find("#");
    return index < 0 ? url : url.substr(0, index);
}

// Convert a bounded supported Godot logical key to the Windows virtual-key representation Steam expects.
uint32_t native_key(Key key) {
    if ((key >= KEY_A && key <= KEY_Z) || (key >= KEY_0 && key <= KEY_9)) {
        return static_cast<uint32_t>(key);
    }
    if (key >= KEY_F1 && key <= KEY_F24) {
        return VK_F1 + key - KEY_F1;
    }
    if (key >= KEY_KP_0 && key <= KEY_KP_9) {
        return VK_NUMPAD0 + key - KEY_KP_0;
    }
    switch (key) {
        case KEY_BACKSPACE: return VK_BACK;
        case KEY_TAB: return VK_TAB;
        case KEY_ENTER: case KEY_KP_ENTER: return VK_RETURN;
        case KEY_SHIFT: return VK_SHIFT;
        case KEY_CTRL: return VK_CONTROL;
        case KEY_ALT: return VK_MENU;
        case KEY_ESCAPE: return VK_ESCAPE;
        case KEY_SPACE: return VK_SPACE;
        case KEY_PAGEUP: return VK_PRIOR;
        case KEY_PAGEDOWN: return VK_NEXT;
        case KEY_END: return VK_END;
        case KEY_HOME: return VK_HOME;
        case KEY_LEFT: return VK_LEFT;
        case KEY_UP: return VK_UP;
        case KEY_RIGHT: return VK_RIGHT;
        case KEY_DOWN: return VK_DOWN;
        case KEY_INSERT: return VK_INSERT;
        case KEY_DELETE: return VK_DELETE;
        case KEY_KP_MULTIPLY: return VK_MULTIPLY;
        case KEY_KP_ADD: return VK_ADD;
        case KEY_KP_SUBTRACT: return VK_SUBTRACT;
        case KEY_KP_PERIOD: return VK_DECIMAL;
        case KEY_KP_DIVIDE: return VK_DIVIDE;
        case KEY_SEMICOLON: return VK_OEM_1;
        case KEY_EQUAL: return VK_OEM_PLUS;
        case KEY_COMMA: return VK_OEM_COMMA;
        case KEY_MINUS: return VK_OEM_MINUS;
        case KEY_PERIOD: return VK_OEM_PERIOD;
        case KEY_SLASH: return VK_OEM_2;
        case KEY_QUOTELEFT: return VK_OEM_3;
        case KEY_BRACKETLEFT: return VK_OEM_4;
        case KEY_BACKSLASH: return VK_OEM_5;
        case KEY_BRACKETRIGHT: return VK_OEM_6;
        case KEY_APOSTROPHE: return VK_OEM_7;
        default: return 0;
    }
}

// Convert the modifier flags supported by the Steam HTML keyboard API.
ISteamHTMLSurface::EHTMLKeyModifiers native_modifiers(const InputEventKey &event) {
    int mask = ISteamHTMLSurface::k_eHTMLKeyModifier_None;
    if (event.is_alt_pressed()) {
        mask |= ISteamHTMLSurface::k_eHTMLKeyModifier_AltDown;
    }
    if (event.is_ctrl_pressed()) {
        mask |= ISteamHTMLSurface::k_eHTMLKeyModifier_CtrlDown;
    }
    if (event.is_shift_pressed()) {
        mask |= ISteamHTMLSurface::k_eHTMLKeyModifier_ShiftDown;
    }
    return static_cast<ISteamHTMLSurface::EHTMLKeyModifiers>(mask);
}

// Map the three pointer buttons supported by the HTML API.
int native_button(MouseButton button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return ISteamHTMLSurface::eHTMLMouseButton_Left;
        case MOUSE_BUTTON_RIGHT: return ISteamHTMLSurface::eHTMLMouseButton_Right;
        case MOUSE_BUTTON_MIDDLE: return ISteamHTMLSurface::eHTMLMouseButton_Middle;
        default: return -1;
    }
}

// Return the exact Godot facade while delivering a native completion to it.
Ref<SteamBrowser> find_browser(ObjectID owner) {
    return Ref<SteamBrowser>(Object::cast_to<SteamBrowser>(ObjectDB::get_instance(owner)));
}

} // namespace
#endif

void SteamBrowserPage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("texture"), &SteamBrowserPage::texture);
    ClassDB::bind_method(D_METHOD("resize", "size"), &SteamBrowserPage::resize);
    ClassDB::bind_method(D_METHOD("navigate", "url"), &SteamBrowserPage::navigate);
    ClassDB::bind_method(D_METHOD("set_focused", "focused"), &SteamBrowserPage::set_focused);
    ClassDB::bind_method(D_METHOD("send_input", "event"), &SteamBrowserPage::send_input);
    ClassDB::bind_method(D_METHOD("close"), &SteamBrowserPage::close);
    ADD_SIGNAL(MethodInfo("page_finished", PropertyInfo(Variant::STRING, "url"), PropertyInfo(Variant::STRING, "title")));
    ADD_SIGNAL(MethodInfo("frame_updated"));
    ADD_SIGNAL(MethodInfo("navigation_blocked", PropertyInfo(Variant::STRING, "url")));
}

std::shared_ptr<SteamRuntime> SteamBrowserPage::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (handle != INVALID_HTMLBROWSER) {
        return live_runtime(runtime, scope);
    }
#endif
    return {};
}

Ref<Texture2D> SteamBrowserPage::texture() const { return active_runtime() ? Ref<Texture2D>(image_texture) : Ref<Texture2D>(); }

Error SteamBrowserPage::resize(const Vector2i &requested) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        if (!valid_steam_html_size(requested)) {
            return ERR_INVALID_PARAMETER;
        }
        if (size != requested) {
            queued_frame.clear();
            size = requested;
            active->get_html_surface()->SetSize(handle, size.x, size.y);
        }
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamBrowserPage::navigate(const String &url) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        if (!permitted(origins, url)) {
            emit_signal("navigation_blocked", url);
            return ERR_UNAUTHORIZED;
        }
        prepare_navigation(url);
        const auto encoded = url.utf8();
        active->get_html_surface()->LoadURL(handle, encoded.get_data(), nullptr);
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamBrowserPage::set_focused(bool requested) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        auto *html = active->get_html_surface();
        if (!requested && focused) {
            for (int button = 0; button < 3; ++button) {
                if (pressed_buttons & (1 << button)) {
                    html->MouseUp(handle, static_cast<ISteamHTMLSurface::EHTMLMouseButton>(button));
                }
            }
            for (const uint32_t key : pressed_keys) {
                html->KeyUp(handle, key, ISteamHTMLSurface::k_eHTMLKeyModifier_None);
            }
            pressed_buttons = 0;
            pressed_keys.clear();
        }
        if (focused != requested) {
            focused = requested;
            html->SetKeyFocus(handle, focused);
        }
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamBrowserPage::send_input(const Ref<InputEvent> &event) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return ERR_UNAVAILABLE;
    }
    if (!focused) {
        return ERR_UNAUTHORIZED;
    }
    auto *html = active->get_html_surface();
    const Ref<InputEventKey> key = event;
    if (key.is_valid()) {
        const uint32_t code = native_key(key->get_keycode());
        const int64_t character = key->get_unicode();
        const auto modifiers = native_modifiers(*key.ptr());
        const bool text = key->is_pressed() && character >= 32 && character != 127 && character <= 0x10ffff &&
                (character < 0xd800 || character > 0xdfff);
        if (code == 0 && !text) {
            return ERR_INVALID_PARAMETER;
        }
        if (code && key->is_pressed()) {
            pressed_keys.insert(code);
            html->KeyDown(handle, code, modifiers, false);
        } else if (code && pressed_keys.erase(code) != 0) {
            html->KeyUp(handle, code, modifiers);
        }
        if (text) {
            html->KeyChar(handle, static_cast<uint32_t>(character), modifiers);
        }
        return OK;
    }
    const Ref<InputEventMouse> mouse = event;
    if (mouse.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    const Vector2 position = mouse->get_position();
    const bool in_bounds = position.is_finite() && position.x >= 0 && position.y >= 0 &&
            position.x < size.x && position.y < size.y;
    const Ref<InputEventMouseButton> button = event;
    const int which = button.is_valid() ? native_button(button->get_button_index()) : -1;
    const bool release = which >= 0 && !button->is_pressed() && (pressed_buttons & (1 << which));
    if (!in_bounds && !release) {
        return ERR_INVALID_PARAMETER;
    }
    if (in_bounds) {
        html->MouseMove(handle, static_cast<int>(position.x), static_cast<int>(position.y));
    }
    if (button.is_null()) {
        return OK;
    }
    if (which >= 0) {
        const auto native = static_cast<ISteamHTMLSurface::EHTMLMouseButton>(which);
        if (button->is_pressed()) {
            pressed_buttons |= 1 << which;
            if (button->is_double_click()) {
                html->MouseDoubleClick(handle, native);
            } else {
                html->MouseDown(handle, native);
            }
        } else if (release) {
            pressed_buttons &= ~(1 << which);
            html->MouseUp(handle, native);
        }
        return OK;
    }
    const MouseButton index = button->get_button_index();
    const float factor = button->get_factor();
    if ((index == MOUSE_BUTTON_WHEEL_UP || index == MOUSE_BUTTON_WHEEL_DOWN) && std::isfinite(factor) && factor > 0) {
        if (button->is_pressed()) {
            const int amount = static_cast<int>(std::round(std::clamp(factor, 0.01f, 10.0f) * 80.0f));
            html->MouseWheel(handle, index == MOUSE_BUTTON_WHEEL_UP ? amount : -amount);
        }
        return OK;
    }
    return ERR_INVALID_PARAMETER;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamBrowserPage::close() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || handle == INVALID_HTMLBROWSER) {
        return;
    }
    const Ref<SteamBrowserPage> self(this);
    const auto service = find_browser(owner);
    const uint32_t native = handle;
    release();
    if (service.is_valid()) {
        service->forget_page(native, ObjectID(get_instance_id()));
    }
#endif
}

void SteamBrowserPage::release() {
#ifdef CRIMSON_WITH_STEAM
    const uint32_t native = std::exchange(handle, INVALID_HTMLBROWSER);
    if (const auto active = runtime.lock()) {
        active->close_html_browser(native);
    }
#endif
    owner = ObjectID();
    runtime.reset();
    scope.reset();
    queued_frame.clear();
    image_texture.unref();
}

void SteamBrowserPage::prepare_navigation(const String &url) {
#ifdef CRIMSON_WITH_STEAM
    if (url == last_navigation || without_fragment(url) != without_fragment(last_navigation)) {
        waiting_for_navigation = have_serial;
        waiting_for_document = true;
        next_serial_authorized = false;
        queued_frame.clear();
    }
#endif
}

void SteamBrowserPage::reject_document(const String &url) {
    close();
    emit_signal("navigation_blocked", url);
}

void SteamBrowserPage::on_html_callback(const SteamHtmlCallback &event) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || handle != event.browser) {
        return;
    }
    auto *html = active->get_html_surface();
    switch (event.kind) {
        case HTML_StartRequest_t::k_iCallback: {
            const String target = event.detail.to_lower();
            const bool same_window = target.is_empty() || target == "_self" || target == "_parent" || target == "_top";
            const bool allow = event.valid_target && same_window && permitted(origins, event.url);
            html->AllowStartRequest(handle, allow);
            if (allow) {
                prepare_navigation(event.url);
                last_navigation = event.url;
            } else {
                waiting_for_navigation = false;
                waiting_for_document = !loaded_document;
                next_serial_authorized = false;
                queued_frame.clear();
                emit_signal("navigation_blocked", event.url);
            }
            break;
        }
        case HTML_URLChanged_t::k_iCallback:
            // Steam can report its internal blank startup document before the requested page loads.
            if (!permitted(origins, event.url) && (loaded_document || event.url != "about:blank")) {
                reject_document(event.url);
            }
            break;
        case HTML_FinishedRequest_t::k_iCallback:
            if (!permitted(origins, event.url)) {
                if (loaded_document || event.url != "about:blank") {
                    reject_document(event.url);
                }
                break;
            }
            last_navigation = event.url;
            loaded_document = true;
            waiting_for_document = false;
            if (have_serial && !waiting_for_navigation) {
                paint_authorized = true;
            } else {
                next_serial_authorized = true;
            }
            emit_signal("page_finished", event.url, event.detail);
            break;
        case HTML_NeedsPaint_t::k_iCallback: {
            const uint32_t difference = event.page_serial - page_serial;
            if (event.size == size && (!have_serial || (difference < 0x80000000u &&
                            (!waiting_for_navigation || difference != 0)))) {
                if (!have_serial || difference != 0) {
                    paint_authorized = next_serial_authorized;
                    next_serial_authorized = false;
                }
                page_serial = event.page_serial;
                have_serial = true;
                waiting_for_navigation = false;
                queued_frame = event.rgba;
            }
            break;
        }
        case HTML_JSAlert_t::k_iCallback:
        case HTML_JSConfirm_t::k_iCallback:
            html->JSDialogResponse(handle, false);
            break;
        case HTML_FileOpenDialog_t::k_iCallback:
            html->FileLoadDialogResponse(handle, nullptr);
            break;
        case HTML_CloseBrowser_t::k_iCallback:
            close();
            break;
        case HTML_OpenLinkInNewTab_t::k_iCallback:
        case HTML_NewWindow_t::k_iCallback:
            emit_signal("navigation_blocked", event.url);
            break;
    }
#endif
}

void SteamBrowserPage::publish_frame() {
#ifdef CRIMSON_WITH_STEAM
    if (!active_runtime() || waiting_for_document || !paint_authorized || queued_frame.is_empty()) {
        return;
    }
    const auto bytes = std::exchange(queued_frame, {});
    const auto image = Image::create_from_data(size.x, size.y, false, Image::FORMAT_RGBA8, bytes);
    if (image.is_null() || image->is_empty()) {
        return;
    }
    const auto target = image_texture;
    if (target->get_width() == size.x && target->get_height() == size.y) {
        target->update(image);
    } else {
        target->set_image(image);
    }
    if (active_runtime()) {
        emit_signal("frame_updated");
    }
#endif
}

void SteamBrowserPageResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamBrowserPageResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamBrowserPage"), "", "get_value");
}

Ref<SteamBrowserPageResult> SteamBrowserPageResult::create(Error error, const String &message,
        const Ref<SteamBrowserPage> &page) {
    Ref<SteamBrowserPageResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = page;
    return result;
}

Ref<SteamBrowserPage> SteamBrowserPageResult::get_value() const { return value; }

void SteamBrowserOpenOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamBrowserPageResult")));
}

void SteamBrowserOpenOperation::finish(const Ref<SteamBrowserPageResult> &result) {
    if (!finished) {
        finished = true;
        emit_signal("completed", result);
    }
}

void SteamBrowserRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "url", "size"), &SteamBrowserRequest::open, DEFVAL(Vector2i(960, 540)));
}

Variant SteamBrowserRequest::open(const String &url, const Vector2i &size) {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto service = find_browser(owner);
        if (service.is_valid()) {
            return service->open_with_origins(url, size, origins);
        }
    }
#endif
    return SteamBrowserPageResult::create(ERR_UNAVAILABLE, "The Steam browser scope is unavailable.");
}

void SteamBrowser::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "url", "size"), &SteamBrowser::open, DEFVAL(Vector2i(960, 540)));
    ClassDB::bind_method(D_METHOD("allow_origins", "origins"), &SteamBrowser::allow_origins);
}

void SteamBrowser::attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &owner) {
    runtime = active;
    scope = owner;
}

void SteamBrowser::detach() {
    runtime.reset();
    scope.reset();
    std::map<uint32_t, Ref<SteamBrowserPage>> closing;
    closing.swap(pages);
    for (const auto &page : closing) {
        page.second->release();
    }
    std::map<uint64_t, Opening> retired;
    retired.swap(opening);
    for (const auto &entry : retired) {
        entry.second.observer->finish(SteamBrowserPageResult::create(ERR_UNAVAILABLE, "The Steam browser scope stopped."));
    }
}

void SteamBrowser::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (!active_runtime()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<Ref<SteamBrowserOpenOperation>> expired;
    for (auto entry = opening.begin(); entry != opening.end();) {
        if (entry->second.deadline <= now) {
            expired.push_back(entry->second.observer);
            entry = opening.erase(entry);
        } else {
            ++entry;
        }
    }
    for (const auto &observer : expired) {
        observer->finish(SteamBrowserPageResult::create(ERR_TIMEOUT, "Steam did not report a ready browser in time."));
    }
    std::vector<Ref<SteamBrowserPage>> snapshot;
    for (const auto &page : pages) {
        snapshot.push_back(page.second);
    }
    for (const auto &page : snapshot) {
        page->publish_frame();
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamBrowser::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    return live_runtime(runtime, scope);
#else
    return {};
#endif
}

Variant SteamBrowser::open(const String &url, const Vector2i &size) {
    return open_with_origins(url, size, PackedStringArray());
}

Ref<SteamBrowserRequest> SteamBrowser::allow_origins(const PackedStringArray &extra) {
    Ref<SteamBrowserRequest> request;
    request.instantiate();
    request->owner = ObjectID(get_instance_id());
    request->origins = extra;
    return request;
}

Variant SteamBrowser::open_with_origins(const String &url, const Vector2i &size, const PackedStringArray &extra) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return SteamBrowserPageResult::create(ERR_UNAVAILABLE, "The Steam browser scope is unavailable.");
    }
    Opening entry;
    if (!valid_steam_html_size(size) || !resolve_policy(url, extra, entry.origins)) {
        return SteamBrowserPageResult::create(ERR_INVALID_PARAMETER, "The browser size, URL, or navigation policy is invalid.");
    }
    const DisplayServer *display = DisplayServer::get_singleton();
    auto *html = display && display->get_name() != "headless" ? active->start_html_surface() : nullptr;
    if (html) {
        if (!active->can_track_call() || !active->reserve_html_browser()) {
            return SteamBrowserPageResult::create(ERR_BUSY, "The process Steam browser budget is full.");
        }
        const SteamAPICall_t call = html->CreateBrowser(nullptr, nullptr);
        const std::weak_ptr<SteamRuntime> weak = active;
        const ObjectID owner(get_instance_id());
        const bool tracking = active->watch_call(call, HTML_BrowserReady_t::k_iCallback, sizeof(HTML_BrowserReady_t),
                [weak, owner, call](const void *record) {
                    const auto runtime = weak.lock();
                    if (!runtime) {
                        return;
                    }
                    HTML_BrowserReady_t ready{};
                    if (record) {
                        std::memcpy(&ready, record, sizeof(ready));
                    }
                    const auto service = find_browser(owner);
                    if (service.is_valid()) {
                        service->complete_open(runtime, call, ready.unBrowserHandle);
                    } else {
                        runtime->discard_html_browser(ready.unBrowserHandle);
                    }
                });
        if (!tracking) {
            active->discard_html_browser(INVALID_HTMLBROWSER);
            return SteamBrowserPageResult::create(ERR_CANT_CREATE, "Steam did not accept the browser create request.");
        }
        entry.url = url;
        entry.size = size;
        entry.observer.instantiate();
        entry.deadline = Time::get_singleton()->get_ticks_msec() + browser_open_timeout_msec;
        const Signal completion(entry.observer.ptr(), "completed");
        opening.emplace(call, std::move(entry));
        return completion;
    }
#endif
    return SteamBrowserPageResult::create(ERR_UNAVAILABLE, "Steam HTML is unavailable in this rendered client scope.");
}

void SteamBrowser::complete_open(const std::shared_ptr<SteamRuntime> &active, uint64_t call, uint32_t handle) {
#ifdef CRIMSON_WITH_STEAM
    const auto found = opening.find(call);
    if (found == opening.end() || active_runtime() != active) {
        active->discard_html_browser(handle);
        return;
    }
    Opening entry = std::move(found->second);
    opening.erase(found);
    if (entry.deadline <= Time::get_singleton()->get_ticks_msec()) {
        active->discard_html_browser(handle);
        entry.observer->finish(SteamBrowserPageResult::create(ERR_TIMEOUT, "Steam did not report a ready browser in time."));
        return;
    }
    if (handle == INVALID_HTMLBROWSER) {
        active->discard_html_browser(handle);
        entry.observer->finish(SteamBrowserPageResult::create(ERR_CANT_CREATE, "Steam could not create a browser."));
        return;
    }
    Ref<SteamBrowserPage> page;
    page.instantiate();
    const ObjectID id(page->get_instance_id());
    const bool registered = active->register_html_browser(handle, [id](const SteamHtmlCallback &event) {
        const Ref<SteamBrowserPage> selected(Object::cast_to<SteamBrowserPage>(ObjectDB::get_instance(id)));
        if (selected.is_valid()) {
            selected->on_html_callback(event);
        }
    });
    if (!registered) {
        active->discard_html_browser(handle);
        entry.observer->finish(SteamBrowserPageResult::create(ERR_CANT_CREATE, "Steam returned a browser already in use."));
        return;
    }
    page->runtime = active;
    page->scope = scope;
    page->owner = ObjectID(get_instance_id());
    page->handle = handle;
    page->size = entry.size;
    page->origins = std::move(entry.origins);
    page->image_texture.instantiate();
    pages.emplace(handle, page);
    auto *html = active->get_html_surface();
    html->SetSize(handle, entry.size.x, entry.size.y);
    const CharString url = entry.url.utf8();
    html->LoadURL(handle, url.get_data(), nullptr);
    entry.observer->finish(SteamBrowserPageResult::create(OK, {}, page));
#endif
}

void SteamBrowser::forget_page(uint32_t handle, ObjectID page) {
    const auto found = pages.find(handle);
    if (found != pages.end() && ObjectID(found->second->get_instance_id()) == page) {
        pages.erase(found);
    }
}

} // namespace crimson::steam
