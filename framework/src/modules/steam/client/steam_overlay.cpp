// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_overlay.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <charconv>
#include <limits>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Bound the URL handed to Steam's overlay browser.
constexpr int64_t maximum_url_bytes = 2048;

// Accept one decimal SteamID64 for an individual account.
bool read_individual_steam_id(const String &text, uint64_t &id) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString encoded = text.utf8();
    const char *first = encoded.get_data();
    const char *last = first + encoded.length();
    const auto parsed = std::from_chars(first, last, id);
    const CSteamID account(id);
    return parsed.ec == std::errc() && parsed.ptr == last && account.IsValid() &&
            account.GetEAccountType() == k_EAccountTypeIndividual;
}

// Reject unsupported schemes, ambiguous authority syntax, and invalid port numbers.
bool valid_web_url(const String &url) {
    const String prefix = url.substr(0, 8).to_lower();
    const int64_t start = prefix.begins_with("https://") ? 8 : prefix.begins_with("http://") ? 7 : 0;
    if (start == 0 || start >= url.length() || url.length() > maximum_url_bytes) {
        return false;
    }
    int64_t authority_end = url.length();
    for (int64_t index = start; index < url.length(); ++index) {
        const char32_t character = url[index];
        if (character <= U' ' || character == U'\x7f' || character == U'\\') {
            return false;
        }
        if (authority_end == url.length() && (character == U'/' || character == U'?' || character == U'#')) {
            authority_end = index;
        }
    }
    const String authority = url.substr(start, authority_end - start);
    if (authority.is_empty() || authority.contains("@")) {
        return false;
    }
    int64_t port_start = authority.find(":");
    if (authority.begins_with("[")) {
        const int64_t close = authority.find("]");
        if (close <= 1) {
            return false;
        }
        const String address = authority.substr(1, close - 1);
        if (!address.contains(":") || !address.is_valid_ip_address()) {
            return false;
        }
        port_start = close + 1 == authority.length() ? -1 : close + 1;
    } else if (port_start == 0 || authority.contains("[") || authority.contains("]")) {
        return false;
    }
    if (port_start == -1) {
        return true;
    }
    const String port = authority.substr(port_start + 1);
    return authority[port_start] == U':' && !port.is_empty() && port.length() <= 5 &&
            port.is_valid_int() && !port.begins_with("+") && port.to_int() > 0 && port.to_int() <= 65535;
}

// Issue a void SDK request only when both its export and the live overlay are available.
template <typename Open, typename... Args>
Error open_pane(const SteamRuntime &runtime, const char *symbol, Args... args) {
    const auto ready = runtime.symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
            "SteamAPI_ISteamUtils_IsOverlayEnabled");
    const auto open = runtime.symbol<Open>(symbol);
    if (!ready || !open) {
        return ERR_UNAVAILABLE;
    }
    if (!ready(runtime.get_utils())) {
        return ERR_BUSY;
    }
    open(runtime.get_friends(), args...);
    return OK;
}

} // namespace
#endif

void SteamOverlay::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ready"), &SteamOverlay::is_ready);
    ClassDB::bind_method(D_METHOD("is_active"), &SteamOverlay::is_active);
    ClassDB::bind_method(D_METHOD("open_achievements"), &SteamOverlay::open_achievements);
    ClassDB::bind_method(D_METHOD("open_store", "app_id"), &SteamOverlay::open_store, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("open_profile", "steam_id"), &SteamOverlay::open_profile);
    ClassDB::bind_method(D_METHOD("open_web", "url"), &SteamOverlay::open_web);
    ClassDB::bind_method(D_METHOD("open_trade", "partner_steam_id"), &SteamOverlay::open_trade);
    ADD_SIGNAL(MethodInfo("activation_changed", PropertyInfo(Variant::BOOL, "active")));
}

bool SteamOverlay::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || activation_subscription || !active || !scope || !scope->active ||
            !active->get_friends() || !active->get_utils() ||
            !active->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>("SteamAPI_ISteamUtils_IsOverlayEnabled")) {
        return false;
    }
    const ObjectID id(get_instance_id());
    activation_subscription = active->subscribe(GameOverlayActivated_t::k_iCallback,
            [id](const void *, size_t) {
                const Ref<SteamOverlay> service(Object::cast_to<SteamOverlay>(ObjectDB::get_instance(id)));
                if (service.is_valid()) {
                    const auto runtime = service->active_runtime();
                    if (runtime) {
                        service->emit_signal("activation_changed", runtime->is_overlay_active());
                    }
                }
            });
    if (!activation_subscription) {
        return false;
    }
    runtime = active;
    client_scope = scope;
    return true;
#else
    return false;
#endif
}

void SteamOverlay::detach() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(activation_subscription);
    }
    activation_subscription = 0;
    runtime.reset();
    client_scope.reset();
#endif
}

std::shared_ptr<SteamRuntime> SteamOverlay::active_runtime() const {
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

Error SteamOverlay::request_runtime(std::shared_ptr<SteamRuntime> &active) const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    active = active_runtime();
    if (active) {
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

bool SteamOverlay::is_ready() const {
#ifdef CRIMSON_WITH_STEAM
    std::shared_ptr<SteamRuntime> active;
    if (request_runtime(active) == OK) {
        const auto ready = active->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
                "SteamAPI_ISteamUtils_IsOverlayEnabled");
        return ready && ready(active->get_utils());
    }
#endif
    return false;
}

bool SteamOverlay::is_active() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return active && active->is_overlay_active();
#else
    return false;
#endif
}

Error SteamOverlay::open_achievements() const {
#ifdef CRIMSON_WITH_STEAM
    std::shared_ptr<SteamRuntime> active;
    const Error status = request_runtime(active);
    if (status == OK) {
        return open_pane<decltype(&SteamAPI_ISteamFriends_ActivateGameOverlay)>(
                *active, "SteamAPI_ISteamFriends_ActivateGameOverlay", "Achievements");
    }
    return status;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamOverlay::open_store(int64_t app_id) const {
#ifdef CRIMSON_WITH_STEAM
    std::shared_ptr<SteamRuntime> active;
    const Error status = request_runtime(active);
    if (status != OK) {
        return status;
    }
    if (app_id < 0 || app_id > (std::numeric_limits<AppId_t>::max)()) {
        return ERR_INVALID_PARAMETER;
    }
    const AppId_t target = app_id == 0 ? active->get_app_id() : static_cast<AppId_t>(app_id);
    return open_pane<decltype(&SteamAPI_ISteamFriends_ActivateGameOverlayToStore)>(
            *active, "SteamAPI_ISteamFriends_ActivateGameOverlayToStore", target, k_EOverlayToStoreFlag_None);
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamOverlay::open_profile(const String &steam_id) const {
    return open_user_pane("steamid", steam_id);
}

Error SteamOverlay::open_web(const String &url) const {
#ifdef CRIMSON_WITH_STEAM
    std::shared_ptr<SteamRuntime> active;
    const Error status = request_runtime(active);
    if (status != OK) {
        return status;
    }
    if (!valid_web_url(url)) {
        return ERR_INVALID_PARAMETER;
    }
    const CharString encoded = url.utf8();
    if (encoded.length() > maximum_url_bytes) {
        return ERR_INVALID_PARAMETER;
    }
    return open_pane<decltype(&SteamAPI_ISteamFriends_ActivateGameOverlayToWebPage)>(
            *active, "SteamAPI_ISteamFriends_ActivateGameOverlayToWebPage", encoded.get_data(),
            k_EActivateGameOverlayToWebPageMode_Default);
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamOverlay::open_trade(const String &partner_steam_id) const {
    return open_user_pane("jointrade", partner_steam_id);
}

Error SteamOverlay::open_user_pane(const char *pane, const String &steam_id) const {
#ifdef CRIMSON_WITH_STEAM
    std::shared_ptr<SteamRuntime> active;
    const Error status = request_runtime(active);
    if (status != OK) {
        return status;
    }
    uint64_t target = 0;
    if (!read_individual_steam_id(steam_id, target)) {
        return ERR_INVALID_PARAMETER;
    }
    return open_pane<decltype(&SteamAPI_ISteamFriends_ActivateGameOverlayToUser)>(
            *active, "SteamAPI_ISteamFriends_ActivateGameOverlayToUser", pane, target);
#else
    return ERR_UNAVAILABLE;
#endif
}

} // namespace crimson::steam
