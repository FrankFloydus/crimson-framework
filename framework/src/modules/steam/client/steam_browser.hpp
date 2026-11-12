// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <set>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
struct SteamHtmlCallback;
class SteamBrowser;

// Scoped offscreen Steam browser and its game-owned texture and input lifecycle.
class SteamBrowserPage : public godot::RefCounted {
    GDCLASS(SteamBrowserPage, godot::RefCounted)

public:
    // Return the texture updated on the scene thread, or null once the page closes.
    godot::Ref<godot::Texture2D> texture() const;
    // Request a bounded surface size in browser pixels.
    godot::Error resize(const godot::Vector2i &size);
    // Request navigation under this page's fixed origin policy.
    godot::Error navigate(const godot::String &url);
    // Give or release input focus while the game's UI owns the browser surface.
    godot::Error set_focused(bool focused);
    // Forward a supported keyboard or mouse event in browser-local coordinates.
    godot::Error send_input(const godot::Ref<godot::InputEvent> &event);
    // Close the browser and release its native resources and routing.
    void close();

protected:
    // Register browser actions and typed content signals for Godot.
    static void _bind_methods();

private:
    friend class SteamBrowser;

    // Resolve this page's exact live client start on the scene thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Process one copied SDK event while the registered handle remains live.
    void on_html_callback(const SteamHtmlCallback &event);
    // Begin a document change without publishing its paints until Steam reports the final allowed URL.
    void prepare_navigation(const godot::String &url);
    // Close an escaped document before delivering a blocked-navigation notification.
    void reject_document(const godot::String &url);
    // Upload at most one queued frame after the shared SDK dispatcher finishes this game frame.
    void publish_frame();
    // Release native ownership without calling back into the owning facade.
    void release();

    // Process runtime retained only by the owning client and native dispatch.
    std::weak_ptr<SteamRuntime> runtime;
    // Original client start generation associated with the native browser.
    std::weak_ptr<SteamClientScope> scope;
    // SteamBrowser tracking this page until explicit close or scope retirement.
    godot::ObjectID owner;
    // Opaque native HTML browser identifier kept inside the framework.
    uint32_t handle = 0;
    // Last requested browser pixel size.
    godot::Vector2i size;
    // Exact allowed origins determined at browser creation.
    godot::PackedStringArray origins;
    // Texture object shared with the game UI while the page is open.
    godot::Ref<godot::ImageTexture> image_texture;
    // Newest frame received in the current SDK pump.
    godot::PackedByteArray queued_frame;
    // URL from the most recent allowed native navigation.
    godot::String last_navigation;
    // Newest accepted native paint serial, used to discard stale draws.
    uint32_t page_serial = 0;
    // Whether the page has accepted at least one paint serial.
    bool have_serial = false;
    // Whether the next paint must belong to a different native navigation serial.
    bool waiting_for_navigation = false;
    // Whether a completed allowed document has ever been observed for this page.
    bool loaded_document = false;
    // Keep paints queued until Steam reports an allowed completed document.
    bool waiting_for_document = true;
    // Whether the queued serial has an allowed final document URL.
    bool paint_authorized = false;
    // An allowed final URL can arrive before the corresponding first paint.
    bool next_serial_authorized = false;
    // Whether the game explicitly gave this page ownership of its keyboard/mouse events.
    bool focused = false;
    // Native mouse buttons still pressed by this UI scope.
    uint8_t pressed_buttons = 0;
    // Native keyboard keys still pressed by this UI scope.
    std::set<uint32_t> pressed_keys;
};

// Terminal creation result with a live page when Steam acknowledged the browser.
class SteamBrowserPageResult : public SteamResult {
    GDCLASS(SteamBrowserPageResult, SteamResult)

public:
    // Create one terminal open outcome before notifying a caller.
    static godot::Ref<SteamBrowserPageResult> create(godot::Error error, const godot::String &message,
            const godot::Ref<SteamBrowserPage> &page = {});
    // Return the page or null when creation failed.
    godot::Ref<SteamBrowserPage> get_value() const;

protected:
    // Register the typed read-only page payload.
    static void _bind_methods();

private:
    // Exact scoped page returned to the caller after native browser readiness.
    godot::Ref<SteamBrowserPage> value;
};

// Internal completion signal for one exact Steam browser creation.
class SteamBrowserOpenOperation : public godot::RefCounted {
    GDCLASS(SteamBrowserOpenOperation, godot::RefCounted)

public:
    // Resume awaiting game code after its open observation settles.
    void finish(const godot::Ref<SteamBrowserPageResult> &result);

protected:
    // Register the internal typed completion signal.
    static void _bind_methods();

private:
    // Ensure one terminal result per observer.
    bool finished = false;
};

// Branchable origin policy for a future browser page.
class SteamBrowserRequest : public godot::RefCounted {
    GDCLASS(SteamBrowserRequest, godot::RefCounted)

public:
    // Open a page using this policy and return its result or awaitable completion.
    godot::Variant open(const godot::String &url, const godot::Vector2i &size = godot::Vector2i(960, 540));

protected:
    // Register the explicit opening terminal.
    static void _bind_methods();

private:
    friend class SteamBrowser;

    // Scoped SteamBrowser resolving native creation at the terminal action.
    godot::ObjectID owner;
    // Additional trusted navigation origins selected by the game.
    godot::PackedStringArray origins;
};

// Creates offscreen browser pages lazily for one client start.
class SteamBrowser : public godot::RefCounted {
    GDCLASS(SteamBrowser, godot::RefCounted)

public:
    // Open a page with default HTTPS and its initial origin.
    godot::Variant open(const godot::String &url, const godot::Vector2i &size = godot::Vector2i(960, 540));
    // Create a branchable policy with additional explicit trusted origins.
    godot::Ref<SteamBrowserRequest> allow_origins(const godot::PackedStringArray &origins);

    // Attach this optional service without initializing Steam HTML yet.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Retire page handles and pending observations while draining native creation separately.
    void detach();
    // Settle local deadlines and publish at most one frame per live page.
    void tick();

protected:
    // Register the default opening action and policy builder.
    static void _bind_methods();

private:
    friend class SteamBrowserPage;
    friend class SteamBrowserRequest;

    // One pending browser creation and its exact scoped observer.
    struct Opening {
        // Validated initial navigation request.
        godot::String url;
        // Canonical origins permitted by this page.
        godot::PackedStringArray origins;
        // Requested browser dimensions.
        godot::Vector2i size;
        // Caller observer retained until timeout, close, or exact readiness.
        godot::Ref<SteamBrowserOpenOperation> observer;
        // Monotonic local deadline for waiting on Steam.
        uint64_t deadline = 0;
    };

    // Resolve this facade's runtime while its original client start is live.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Validate policy and begin a Steam browser creation.
    godot::Variant open_with_origins(const godot::String &url, const godot::Vector2i &size,
            const godot::PackedStringArray &origins);
    // Adopt the exact returned handle or discard it if its observer has retired.
    void complete_open(const std::shared_ptr<SteamRuntime> &active, uint64_t call, uint32_t handle);
    // Drop ownership of a closed page only when the handle still matches that exact object.
    void forget_page(uint32_t handle, godot::ObjectID page);

    // Runtime observed without extending process shutdown.
    std::weak_ptr<SteamRuntime> runtime;
    // The facade is bound to its original client start only.
    std::weak_ptr<SteamClientScope> scope;
    // In-flight local observations keyed by native create call.
    std::map<uint64_t, Opening> opening;
    // Pages retained until close or the owning start stops.
    std::map<uint32_t, godot::Ref<SteamBrowserPage>> pages;
};

} // namespace crimson::steam
