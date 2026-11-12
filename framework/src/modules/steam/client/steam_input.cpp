// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_input.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/client/steam_input_session.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <cstring>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

#ifdef CRIMSON_WITH_STEAM
// Steam consumes a loose on-disk manifest rather than a Godot resource path.
bool valid_manifest_path(const String &path) {
    if (path.is_empty()) {
        return true;
    }
    const CharString bytes = path.utf8();
    return path.is_absolute_path() && !path.contains("://") && bytes.length() <= 4096 &&
            std::strlen(bytes.get_data()) == static_cast<size_t>(bytes.length());
}

// Decode the big-endian PNG dimension without reading outside the checked header.
uint32_t png_dimension(const uint8_t *bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
            (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

// Load a bounded Steam glyph PNG into a detached Godot texture.
Ref<Texture2D> load_glyph(const String &path) {
    const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    constexpr uint64_t max_bytes = 1024 * 1024;
    const uint64_t length = file.is_valid() ? file->get_length() : 0;
    if (length < 24 || length > max_bytes) {
        return {};
    }
    const auto bytes = file->get_buffer(static_cast<int64_t>(length));
    constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    const uint8_t *data = bytes.ptr();
    if (bytes.size() < 24 || std::memcmp(data, signature, sizeof(signature)) != 0 ||
            std::memcmp(data + 12, "IHDR", 4) != 0) {
        return {};
    }
    const uint32_t width = png_dimension(data + 16);
    const uint32_t height = png_dimension(data + 20);
    if (width == 0 || height == 0 || width > 512 || height > 512) {
        return {};
    }
    Ref<Image> image;
    image.instantiate();
    if (image->load_png_from_buffer(bytes) != OK || image->is_empty()) {
        return {};
    }
    return ImageTexture::create_from_image(image);
}
#endif

} // namespace

void SteamInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start", "manifest_path"), &SteamInput::start, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("stop"), &SteamInput::stop);
    ClassDB::bind_method(D_METHOD("controllers"), &SteamInput::controllers);

    const PropertyInfo controller(Variant::OBJECT, "controller", PROPERTY_HINT_RESOURCE_TYPE,
            SteamController::get_class_static());
    ADD_SIGNAL(MethodInfo("controller_connected", controller));
    ADD_SIGNAL(MethodInfo("controller_disconnected", controller));
    ADD_SIGNAL(MethodInfo("bindings_changed", controller));
}

void SteamInput::attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope) {
    runtime = active;
    client_scope = scope;
}

void SteamInput::detach() {
    client_scope.reset();
    runtime.reset();
    stop();
}

std::shared_ptr<SteamRuntime> SteamInput::active_runtime() const {
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

Error SteamInput::start(const String &manifest_path) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_manifest_path(manifest_path)) {
        return ERR_INVALID_PARAMETER;
    }
    const String path = manifest_path.is_empty() ? String() : manifest_path.simplify_path();
    Error error = OK;
    auto next = SteamInputSession::acquire(active, path, error);
    if (!next || session == next) {
        return error;
    }
    session = std::move(next);
    ++lease_generation;
    tick();
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamInput::stop() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return;
    }
    ++lease_generation;
    views.clear();
    glyph_cache.clear();
    session.reset();
#endif
}

TypedArray<SteamController> SteamInput::controllers() const {
    TypedArray<SteamController> snapshot;
#ifdef CRIMSON_WITH_STEAM
    if (active_runtime() && session) {
        for (const auto &entry : views) {
            if (owns_controller(*entry.second.ptr())) {
                snapshot.push_back(entry.second);
            }
        }
    }
#endif
    snapshot.make_read_only();
    return snapshot;
}

bool SteamInput::owns_controller(const SteamController &controller) const {
#ifdef CRIMSON_WITH_STEAM
    if (active_runtime() && session) {
        const auto view = views.find(controller.handle);
        const auto &connected = session->connections();
        const auto device = connected.find(controller.handle);
        return view != views.end() && view->second.ptr() == &controller &&
                device != connected.end() && device->second.identity == controller.connection;
    }
#endif
    return false;
}

void SteamInput::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (!session || !active_runtime()) {
        return;
    }
    const Ref<SteamInput> keep_alive(this);
    const uint64_t generation = lease_generation;
    session->poll();
    const auto &connected = session->connections();
    std::vector<Ref<SteamController>> removed;
    std::vector<Ref<SteamController>> added;
    std::vector<Ref<SteamController>> rebound;
    for (auto view = views.begin(); view != views.end();) {
        const auto device = connected.find(view->first);
        if (device == connected.end() || device->second.identity != view->second->connection) {
            removed.push_back(view->second);
            view = views.erase(view);
            continue;
        }
        if (view->second->binding_revision != device->second.binding_revision) {
            view->second->binding_revision = device->second.binding_revision;
            rebound.push_back(view->second);
        }
        ++view;
    }
    for (const auto &device : connected) {
        if (views.count(device.first) != 0) {
            continue;
        }
        Ref<SteamController> view;
        view.instantiate();
        view->owner = ObjectID(get_instance_id());
        view->handle = device.first;
        view->connection = device.second.identity;
        view->binding_revision = device.second.binding_revision;
        views.emplace(device.first, view);
        added.push_back(view);
    }
    if (!removed.empty() || !rebound.empty()) {
        glyph_cache.clear();
    }
    for (const auto &view : removed) {
        if (generation != lease_generation || !active_runtime()) {
            return;
        }
        emit_signal("controller_disconnected", view);
    }
    for (const auto &view : added) {
        if (generation != lease_generation || !active_runtime()) {
            return;
        }
        emit_signal("controller_connected", view);
    }
    for (const auto &view : rebound) {
        if (generation != lease_generation || !active_runtime()) {
            return;
        }
        emit_signal("bindings_changed", view);
    }
#endif
}

Ref<Texture2D> SteamInput::glyph_texture(int origin, int size) {
#ifdef CRIMSON_WITH_STEAM
    const auto key = std::make_pair(origin, size);
    const auto cached = glyph_cache.find(key);
    if (cached != glyph_cache.end()) {
        return cached->second;
    }
    const auto get_path = session->get_runtime().symbol<decltype(&SteamAPI_ISteamInput_GetGlyphPNGForActionOrigin)>(
            "SteamAPI_ISteamInput_GetGlyphPNGForActionOrigin");
    const char *path = get_path ? get_path(session->interface(), static_cast<EInputActionOrigin>(origin),
            static_cast<ESteamInputGlyphSize>(size), ESteamInputGlyphStyle_Knockout) : nullptr;
    const Ref<Texture2D> texture = path && *path ? load_glyph(String::utf8(path)) : Ref<Texture2D>();
    if (texture.is_valid() && glyph_cache.size() < 64) {
        glyph_cache.emplace(key, texture);
    }
    return texture;
#else
    return {};
#endif
}

} // namespace crimson::steam
