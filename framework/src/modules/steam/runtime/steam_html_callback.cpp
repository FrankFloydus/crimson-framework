// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/runtime/steam_html_callback.hpp"

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <cstring>

namespace crimson::steam {
using namespace godot;

namespace {

// Limit the URL, title, and target read from Steam's nul-terminated callback strings.
constexpr size_t maximum_url_bytes = 2048;
constexpr size_t maximum_title_bytes = 1024;
constexpr size_t maximum_target_bytes = 64;
// Limit the maximum owned texture allocation per browser and per paint.
constexpr uint32_t maximum_dimension = 4096;
constexpr int64_t maximum_pixels = 4 * 1024 * 1024;

// Copy only exact records for handles created by the shared runtime.
template <typename Native>
bool read_record(const SteamRuntime &active, const void *record, size_t size, Native &out) {
    if (!record || size != sizeof(Native)) {
        return false;
    }
    std::memcpy(&out, record, size);
    return active.owns_html_browser(out.unBrowserHandle);
}

// Decode a terminated string without reading its memory past the configured byte budget.
bool copy_text(const char *source, size_t limit, String &out) {
    if (!source) {
        return false;
    }
    for (size_t length = 0; length <= limit; ++length) {
        if (source[length] == '\0') {
            return length == 0 || out.parse_utf8(source, static_cast<int64_t>(length)) == OK;
        }
    }
    return false;
}

// Copy a bounded complete BGRA frame as RGBA while the SDK still owns the source pointer.
void copy_paint(SteamRuntime &active, const HTML_NeedsPaint_t &paint, SteamHtmlCallback &owned) {
    if (!paint.pBGRA || paint.unWide > maximum_dimension || paint.unTall > maximum_dimension) {
        return;
    }
    const Vector2i size(paint.unWide, paint.unTall);
    if (!valid_steam_html_size(size) || !active.claim_html_paint(paint.unBrowserHandle)) {
        return;
    }
    const int64_t bytes = int64_t(size.x) * size.y * 4;
    if (owned.rgba.resize(bytes) != OK) {
        return;
    }
    auto *destination = owned.rgba.ptrw();
    const auto *source = reinterpret_cast<const uint8_t *>(paint.pBGRA);
    for (int64_t offset = 0; offset < bytes; offset += 4) {
        destination[offset] = source[offset + 2];
        destination[offset + 1] = source[offset + 1];
        destination[offset + 2] = source[offset];
        destination[offset + 3] = source[offset + 3];
    }
    owned.browser = paint.unBrowserHandle;
    owned.page_serial = paint.unPageSerial;
    owned.size = size;
}

// Only the browser handle is needed to dismiss an SDK dialog or close request.
template <typename Native>
uint32_t copy_handle(const SteamRuntime &active, const void *record, size_t size) {
    Native native{};
    return read_record(active, record, size, native) ? native.unBrowserHandle : 0;
}

} // namespace

bool valid_steam_html_size(const Vector2i &size) {
    return size.x > 0 && size.y > 0 && size.x <= int(maximum_dimension) && size.y <= int(maximum_dimension) &&
            int64_t(size.x) * size.y <= maximum_pixels;
}

bool is_steam_html_callback(int kind) {
    switch (kind) {
        case HTML_NeedsPaint_t::k_iCallback:
        case HTML_StartRequest_t::k_iCallback:
        case HTML_CloseBrowser_t::k_iCallback:
        case HTML_URLChanged_t::k_iCallback:
        case HTML_FinishedRequest_t::k_iCallback:
        case HTML_OpenLinkInNewTab_t::k_iCallback:
        case HTML_NewWindow_t::k_iCallback:
        case HTML_JSAlert_t::k_iCallback:
        case HTML_JSConfirm_t::k_iCallback:
        case HTML_FileOpenDialog_t::k_iCallback:
            return true;
        default:
            return false;
    }
}

SteamHtmlCallback copy_steam_html_callback(SteamRuntime &active, int kind, const void *record, size_t size) {
    SteamHtmlCallback owned;
    owned.kind = kind;
    switch (kind) {
        case HTML_NeedsPaint_t::k_iCallback: {
            HTML_NeedsPaint_t paint{};
            if (read_record(active, record, size, paint)) {
                copy_paint(active, paint, owned);
            }
            break;
        }
        case HTML_StartRequest_t::k_iCallback: {
            HTML_StartRequest_t request{};
            if (read_record(active, record, size, request)) {
                owned.browser = request.unBrowserHandle;
                if (!copy_text(request.pchURL, maximum_url_bytes, owned.url)) {
                    owned.url = String();
                }
                owned.valid_target = !request.pchTarget || copy_text(request.pchTarget, maximum_target_bytes, owned.detail);
            }
            break;
        }
        case HTML_FinishedRequest_t::k_iCallback: {
            HTML_FinishedRequest_t finished{};
            if (read_record(active, record, size, finished)) {
                owned.browser = finished.unBrowserHandle;
                if (!copy_text(finished.pchURL, maximum_url_bytes, owned.url)) {
                    owned.url = String();
                }
                if (!copy_text(finished.pchPageTitle, maximum_title_bytes, owned.detail)) {
                    owned.detail = String();
                }
            }
            break;
        }
        case HTML_URLChanged_t::k_iCallback: {
            HTML_URLChanged_t changed{};
            if (read_record(active, record, size, changed)) {
                owned.browser = changed.unBrowserHandle;
                if (!copy_text(changed.pchURL, maximum_url_bytes, owned.url)) {
                    owned.url = String();
                }
            }
            break;
        }
        case HTML_OpenLinkInNewTab_t::k_iCallback: {
            HTML_OpenLinkInNewTab_t popup{};
            if (read_record(active, record, size, popup)) {
                owned.browser = popup.unBrowserHandle;
                if (!copy_text(popup.pchURL, maximum_url_bytes, owned.url)) {
                    owned.url = String();
                }
            }
            break;
        }
        case HTML_NewWindow_t::k_iCallback: {
            HTML_NewWindow_t popup{};
            if (read_record(active, record, size, popup)) {
                owned.browser = popup.unBrowserHandle;
                // Steam destroys unNewWindow_BrowserHandle_IGNORE immediately in SDK 1.65.
                if (!copy_text(popup.pchURL, maximum_url_bytes, owned.url)) {
                    owned.url = String();
                }
            }
            break;
        }
        case HTML_CloseBrowser_t::k_iCallback:
            owned.browser = copy_handle<HTML_CloseBrowser_t>(active, record, size);
            break;
        case HTML_JSAlert_t::k_iCallback:
            owned.browser = copy_handle<HTML_JSAlert_t>(active, record, size);
            break;
        case HTML_JSConfirm_t::k_iCallback:
            owned.browser = copy_handle<HTML_JSConfirm_t>(active, record, size);
            break;
        case HTML_FileOpenDialog_t::k_iCallback:
            owned.browser = copy_handle<HTML_FileOpenDialog_t>(active, record, size);
            break;
    }
    return owned;
}

} // namespace crimson::steam
#endif
