// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_apps.hpp"
#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include <limits>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_ids.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Snapshot properties are read-only and are never saved as instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

// Accept only nonzero AppIDs representable by Steam's unsigned 32-bit AppId_t.
bool valid_app_id(int64_t app_id) {
    return app_id > 0 && app_id <= (std::numeric_limits<uint32_t>::max)();
}

#ifdef CRIMSON_WITH_STEAM
// Bound the number of catalog entries copied even when Steam reports more.
constexpr int maximum_dlc_entries = 256;
// Bound each copied DLC display name including the native terminator.
constexpr int dlc_name_buffer_bytes = 512;

// Resolve Apps lazily so optional metadata support does not gate client startup.
ISteamApps *get_apps(const std::shared_ptr<SteamRuntime> &runtime) {
    if (!runtime) {
        return nullptr;
    }
    const auto find = runtime->symbol<decltype(&SteamAPI_SteamApps_v009)>("SteamAPI_SteamApps_v009");
    return find ? find() : nullptr;
}
#endif

} // namespace

void SteamDlcInfo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamDlcInfo::get_app_id);
    ClassDB::bind_method(D_METHOD("get_display_name"), &SteamDlcInfo::get_display_name);
    ClassDB::bind_method(D_METHOD("is_available"), &SteamDlcInfo::is_available);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name", PROPERTY_HINT_NONE, "", read_only), "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "available", PROPERTY_HINT_NONE, "", read_only), "", "is_available");
}

int64_t SteamDlcInfo::get_app_id() const {
    return app_id;
}

String SteamDlcInfo::get_display_name() const {
    return display_name;
}

bool SteamDlcInfo::is_available() const {
    return available;
}

void SteamDlcProgress::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_active"), &SteamDlcProgress::is_active);
    ClassDB::bind_method(D_METHOD("get_downloaded_bytes"), &SteamDlcProgress::get_downloaded_bytes);
    ClassDB::bind_method(D_METHOD("get_total_bytes"), &SteamDlcProgress::get_total_bytes);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active", PROPERTY_HINT_NONE, "", read_only), "", "is_active");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "downloaded_bytes", PROPERTY_HINT_NONE, "", read_only),
            "", "get_downloaded_bytes");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total_bytes", PROPERTY_HINT_NONE, "", read_only), "", "get_total_bytes");
}

bool SteamDlcProgress::is_active() const {
    return active;
}

int64_t SteamDlcProgress::get_downloaded_bytes() const {
    return downloaded_bytes;
}

int64_t SteamDlcProgress::get_total_bytes() const {
    return total_bytes;
}

void SteamDlcListResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamDlcListResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, "SteamDlcInfo", read_only),
            "", "get_value");
}

TypedArray<SteamDlcInfo> SteamDlcListResult::get_value() const {
    return items.duplicate();
}

Ref<SteamDlcListResult> SteamDlcListResult::create(Error error, const String &message,
        const TypedArray<SteamDlcInfo> &items) {
    Ref<SteamDlcListResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    if (error == OK) {
        result->items = items;
    }
    return result;
}

void SteamDlcProgressResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamDlcProgressResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamDlcProgress", read_only),
            "", "get_value");
}

Ref<SteamDlcProgress> SteamDlcProgressResult::get_value() const {
    return value;
}

Ref<SteamDlcProgressResult> SteamDlcProgressResult::create(Error error, const String &message,
        bool active, int64_t downloaded_bytes, int64_t total_bytes) {
    Ref<SteamDlcProgressResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    if (error == OK) {
        result->value.instantiate();
        result->value->active = active;
        result->value->downloaded_bytes = downloaded_bytes;
        result->value->total_bytes = total_bytes;
    }
    return result;
}

void SteamApps::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamApps::is_available);
    ClassDB::bind_method(D_METHOD("current_language"), &SteamApps::current_language);
    ClassDB::bind_method(D_METHOD("available_languages"), &SteamApps::available_languages);
    ClassDB::bind_method(D_METHOD("beta_branch"), &SteamApps::beta_branch);
    ClassDB::bind_method(D_METHOD("build_id"), &SteamApps::build_id);
    ClassDB::bind_method(D_METHOD("notification_session_id"), &SteamApps::notification_session_id);
    ClassDB::bind_method(D_METHOD("is_subscribed_to_app", "app_id"), &SteamApps::is_subscribed_to_app);
    ClassDB::bind_method(D_METHOD("owns_dlc", "dlc_app_id"), &SteamApps::owns_dlc);
    ClassDB::bind_method(D_METHOD("is_dlc_installed", "app_id"), &SteamApps::is_dlc_installed);
    ClassDB::bind_method(D_METHOD("dlc"), &SteamApps::dlc);
    ClassDB::bind_method(D_METHOD("install_dlc", "dlc_app_id"), &SteamApps::install_dlc);
    ClassDB::bind_method(D_METHOD("uninstall_dlc", "dlc_app_id"), &SteamApps::uninstall_dlc);
    ClassDB::bind_method(D_METHOD("download_progress", "dlc_app_id"), &SteamApps::download_progress);
    ADD_SIGNAL(MethodInfo("dlc_installed", PropertyInfo(Variant::INT, "dlc_app_id")));
    ADD_SIGNAL(MethodInfo("notification_session_requested", PropertyInfo(Variant::STRING, "session_id")));
}

void SteamApps::attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    runtime = active;
    client_scope = scope;
    if (!get_apps(active)) {
        return;
    }
    last_notification_session = notification_session_id();
    const ObjectID id(get_instance_id());
    const std::weak_ptr<SteamClientScope> observed = scope;
    installed_subscription = active->subscribe(DlcInstalled_t::k_iCallback, [id, observed](const void *record, size_t size) {
        const auto active_scope = observed.lock();
        if (!record || size != sizeof(DlcInstalled_t) || !active_scope || !active_scope->active) {
            return;
        }
        DlcInstalled_t event{};
        std::memcpy(&event, record, sizeof(event));
        const Ref<SteamApps> service(Object::cast_to<SteamApps>(ObjectDB::get_instance(id)));
        if (service.is_valid() && service->client_scope.lock() == active_scope && valid_app_id(event.m_nAppID)) {
            service->emit_signal("dlc_installed", static_cast<int64_t>(event.m_nAppID));
        }
    });
    notification_subscription = active->subscribe(NewUrlLaunchParameters_t::k_iCallback,
            [id, observed](const void *, size_t size) {
                const auto expected = observed.lock();
                const Ref<SteamApps> service(Object::cast_to<SteamApps>(ObjectDB::get_instance(id)));
                if ((size != 0 && size != sizeof(NewUrlLaunchParameters_t)) || !expected || !expected->active ||
                        service.is_null() || service->client_scope.lock() != expected) {
                    return;
                }
                const String session = service->notification_session_id();
                if (session == service->last_notification_session) {
                    return;
                }
                service->last_notification_session = session;
                if (!session.is_empty()) {
                    service->emit_signal("notification_session_requested", session);
                }
            });
#endif
}

void SteamApps::detach() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(installed_subscription);
        active->unsubscribe(notification_subscription);
    }
    installed_subscription = 0;
    notification_subscription = 0;
    last_notification_session = String();
    runtime.reset();
    client_scope.reset();
#endif
}

std::shared_ptr<SteamRuntime> SteamApps::active_runtime() const {
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

bool SteamApps::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return get_apps(active_runtime()) != nullptr;
#else
    return false;
#endif
}

String SteamApps::notification_session_id() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamApps *apps = get_apps(active);
    const auto read = apps ? active->symbol<decltype(&SteamAPI_ISteamApps_GetLaunchQueryParam)>(
            "SteamAPI_ISteamApps_GetLaunchQueryParam") : nullptr;
    const char *raw = read ? read(apps, "_sessionid") : nullptr;
    if (!raw) {
        return {};
    }
    size_t length = 0;
    while (length <= 20 && raw[length] != '\0') {
        ++length;
    }
    if (length > 20) {
        return {};
    }
    const String session = String::utf8(raw, static_cast<int64_t>(length));
    uint64_t value = 0;
    if (parse_steam_id(session, value) && session == String::num_uint64(value)) {
        return session;
    }
#endif
    return {};
}

String SteamApps::current_language() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_GetCurrentGameLanguage)>(
                "SteamAPI_ISteamApps_GetCurrentGameLanguage");
        const char *language = read ? read(apps) : nullptr;
        return language ? String::utf8(language) : String();
    }
#endif
    return {};
}

PackedStringArray SteamApps::available_languages() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_GetAvailableGameLanguages)>(
                "SteamAPI_ISteamApps_GetAvailableGameLanguages");
        if (const char *languages = read ? read(apps) : nullptr) {
            return String::utf8(languages).split(",", false);
        }
    }
#endif
    return {};
}

String SteamApps::beta_branch() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_GetCurrentBetaName)>(
                "SteamAPI_ISteamApps_GetCurrentBetaName");
        std::array<char, 256> name{};
        if (read && read(apps, name.data(), static_cast<int>(name.size()))) {
            name.back() = '\0';
            return String::utf8(name.data());
        }
    }
#endif
    return {};
}

int64_t SteamApps::build_id() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_GetAppBuildId)>(
                "SteamAPI_ISteamApps_GetAppBuildId");
        return read ? (std::max)(0, read(apps)) : 0;
    }
#endif
    return 0;
}

bool SteamApps::is_subscribed_to_app(int64_t app_id) const {
#ifdef CRIMSON_WITH_STEAM
    if (valid_app_id(app_id)) {
        const auto active = active_runtime();
        if (ISteamApps *apps = get_apps(active)) {
            const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_BIsSubscribedApp)>(
                    "SteamAPI_ISteamApps_BIsSubscribedApp");
            return read && read(apps, static_cast<AppId_t>(app_id));
        }
    }
#endif
    return false;
}

bool SteamApps::owns_dlc(int64_t dlc_app_id) const {
    return is_subscribed_to_app(dlc_app_id);
}

bool SteamApps::is_dlc_installed(int64_t app_id) const {
#ifdef CRIMSON_WITH_STEAM
    if (valid_app_id(app_id)) {
        const auto active = active_runtime();
        if (ISteamApps *apps = get_apps(active)) {
            const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_BIsDlcInstalled)>(
                    "SteamAPI_ISteamApps_BIsDlcInstalled");
            return read && read(apps, static_cast<AppId_t>(app_id));
        }
    }
#endif
    return false;
}

Ref<SteamDlcListResult> SteamApps::dlc() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto read_count = active->symbol<decltype(&SteamAPI_ISteamApps_GetDLCCount)>(
                "SteamAPI_ISteamApps_GetDLCCount");
        const auto read_entry = active->symbol<decltype(&SteamAPI_ISteamApps_BGetDLCDataByIndex)>(
                "SteamAPI_ISteamApps_BGetDLCDataByIndex");
        if (read_count && read_entry) {
            const int reported_count = read_count(apps);
            if (reported_count < 0) {
                return SteamDlcListResult::create(ERR_INVALID_DATA, "Steam returned an invalid DLC count.");
            }
            TypedArray<SteamDlcInfo> entries;
            const int count = (std::min)(reported_count, maximum_dlc_entries);
            for (int index = 0; index < count; ++index) {
                AppId_t app_id = 0;
                bool available = false;
                std::array<char, dlc_name_buffer_bytes> name{};
                if (!read_entry(apps, index, &app_id, &available, name.data(), dlc_name_buffer_bytes) ||
                        !valid_app_id(app_id)) {
                    return SteamDlcListResult::create(ERR_INVALID_DATA, "Steam returned an invalid DLC catalog entry.");
                }
                name.back() = '\0';
                Ref<SteamDlcInfo> entry;
                entry.instantiate();
                entry->app_id = app_id;
                entry->display_name = String::utf8(name.data());
                entry->available = available;
                entries.push_back(entry);
            }
            return SteamDlcListResult::create(OK, {}, entries);
        }
    }
#endif
    return SteamDlcListResult::create(ERR_UNAVAILABLE, "Steam DLC listing is unavailable.");
}

Error SteamApps::install_dlc(int64_t dlc_app_id) {
    if (!valid_app_id(dlc_app_id)) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto owned = active->symbol<decltype(&SteamAPI_ISteamApps_BIsSubscribedApp)>(
                "SteamAPI_ISteamApps_BIsSubscribedApp");
        const auto install = active->symbol<decltype(&SteamAPI_ISteamApps_InstallDLC)>("SteamAPI_ISteamApps_InstallDLC");
        if (owned && install) {
            const auto app_id = static_cast<AppId_t>(dlc_app_id);
            if (!owned(apps, app_id)) {
                return ERR_UNAUTHORIZED;
            }
            install(apps, app_id);
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamApps::uninstall_dlc(int64_t dlc_app_id) {
    if (!valid_app_id(dlc_app_id)) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto uninstall = active->symbol<decltype(&SteamAPI_ISteamApps_UninstallDLC)>(
                "SteamAPI_ISteamApps_UninstallDLC");
        if (uninstall) {
            uninstall(apps, static_cast<AppId_t>(dlc_app_id));
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Ref<SteamDlcProgressResult> SteamApps::download_progress(int64_t dlc_app_id) const {
    if (!valid_app_id(dlc_app_id)) {
        return SteamDlcProgressResult::create(ERR_INVALID_PARAMETER, "Provide a positive Steam DLC AppID.");
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamApps *apps = get_apps(active)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamApps_GetDlcDownloadProgress)>(
                "SteamAPI_ISteamApps_GetDlcDownloadProgress");
        if (read) {
            uint64 downloaded = 0;
            uint64 total = 0;
            if (!read(apps, static_cast<AppId_t>(dlc_app_id), &downloaded, &total)) {
                return SteamDlcProgressResult::create(OK, {});
            }
            const auto maximum_bytes = static_cast<uint64>((std::numeric_limits<int64_t>::max)());
            if (downloaded > maximum_bytes || total > maximum_bytes) {
                return SteamDlcProgressResult::create(ERR_INVALID_DATA, "Steam DLC progress exceeds the supported byte range.");
            }
            return SteamDlcProgressResult::create(OK, {}, true, static_cast<int64_t>(downloaded), static_cast<int64_t>(total));
        }
    }
#endif
    return SteamDlcProgressResult::create(ERR_UNAVAILABLE, "Steam DLC progress is unavailable.");
}

} // namespace crimson::steam
