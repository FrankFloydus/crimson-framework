// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_screenshots.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_ids.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <cstring>
#include <utility>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Reject oversized images before duplicating or converting their pixels.
constexpr int64_t maximum_pixels = 16 * 1024 * 1024;
// Bound dimension independently of the combined pixel limit.
constexpr int32_t maximum_dimension = 8192;
// The SDK allows this many tags of each kind on one screenshot.
constexpr int64_t maximum_tags = 32;

// Resolve and retain the original service without extending its client lifetime.
Ref<SteamScreenshots> find_screenshots(ObjectID owner) {
    return Ref<SteamScreenshots>(Object::cast_to<SteamScreenshots>(ObjectDB::get_instance(owner)));
}

#ifdef CRIMSON_WITH_STEAM

// Limit retained writes when several independent photo drafts are submitted together.
constexpr size_t maximum_pending = 16;
// Bound local observation when the Steam client does not report an outcome.
constexpr uint64_t observation_timeout_msec = 15000;

static_assert(maximum_tags == k_nScreenshotMaxTaggedUsers);
static_assert(maximum_tags == k_nScreenshotMaxTaggedPublishedFiles);

// Resolve the optional screenshot interface from the runtime's exact SDK.
ISteamScreenshots *get_screenshots(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamScreenshots_v003)>(
                                       "SteamAPI_SteamScreenshots_v003")
                             : nullptr;
    return get ? get() : nullptr;
}

// Validate and deduplicate tags before any screenshot is written.
bool read_tags(const PackedStringArray &raw, bool individuals, std::vector<uint64_t> &tags) {
    for (int64_t index = 0; index < raw.size(); ++index) {
        uint64_t id = 0;
        if (!(individuals ? parse_steam_account(raw[index], id) : parse_steam_id(raw[index], id))) {
            return false;
        }
        if (std::find(tags.begin(), tags.end(), id) == tags.end()) {
            tags.push_back(id);
        }
    }
    return true;
}

#endif

} // namespace

void SteamScreenshot::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_annotations_accepted"), &SteamScreenshot::get_annotations_accepted);
    ClassDB::bind_method(D_METHOD("get_annotation_errors"), &SteamScreenshot::get_annotation_errors);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "annotations_accepted"), "", "get_annotations_accepted");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "annotation_errors", PROPERTY_HINT_ARRAY_TYPE, "int"),
            "", "get_annotation_errors");
}

bool SteamScreenshot::get_annotations_accepted() const { return errors.is_empty(); }
TypedArray<int64_t> SteamScreenshot::get_annotation_errors() const { return errors; }

void SteamScreenshotResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamScreenshotResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamScreenshot"), "", "get_value");
}

Ref<SteamScreenshot> SteamScreenshotResult::get_value() const { return value; }

Ref<SteamScreenshotResult> SteamScreenshotResult::create(Error error, const String &message,
        const Ref<SteamScreenshot> &screenshot) {
    Ref<SteamScreenshotResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = screenshot;
    return result;
}

void SteamScreenshotOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamScreenshotOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamScreenshotOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamScreenshotOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamScreenshotOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamScreenshotResult")));
}

bool SteamScreenshotOperation::is_finished() const { return outcome.is_valid(); }
Ref<SteamScreenshotResult> SteamScreenshotOperation::result() const { return outcome; }
Variant SteamScreenshotOperation::wait() { return is_finished() ? Variant(outcome) : Variant(Signal(this, "completed")); }

Error SteamScreenshotOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (handle != 0) {
        const auto service = find_screenshots(owner);
        if (service.is_valid()) {
            return service->cancel(handle);
        }
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

void SteamScreenshotOperation::finish(Error error, const String &message, const Ref<SteamScreenshot> &screenshot) {
    if (!is_finished()) {
        owner = ObjectID();
        handle = 0;
        outcome = SteamScreenshotResult::create(error, message, screenshot);
        emit_signal("completed", outcome);
    }
}

void SteamScreenshotRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("at", "location"), &SteamScreenshotRequest::at);
    ClassDB::bind_method(D_METHOD("tag", "steam_id"), &SteamScreenshotRequest::tag);
    ClassDB::bind_method(D_METHOD("tag_item", "published_id"), &SteamScreenshotRequest::tag_item);
    ClassDB::bind_method(D_METHOD("submit"), &SteamScreenshotRequest::submit);
    ClassDB::bind_method(D_METHOD("send"), &SteamScreenshotRequest::send);
}

Ref<SteamScreenshotRequest> SteamScreenshotRequest::branch() const {
    Ref<SteamScreenshotRequest> next;
    next.instantiate();
    next->owner = owner;
    next->pixels = pixels;
    next->size = size;
    next->location = location;
    next->users = users;
    next->items = items;
    next->draft_error = draft_error;
    return next;
}

Ref<SteamScreenshotRequest> SteamScreenshotRequest::at(const String &value) const {
    auto next = branch();
    next->location = value;
    return next;
}

Ref<SteamScreenshotRequest> SteamScreenshotRequest::tag(const String &steam_id) const {
    auto next = branch();
    if (next->users.size() < maximum_tags) {
        next->users.append(steam_id);
    } else if (next->draft_error == OK) {
        next->draft_error = ERR_INVALID_PARAMETER;
    }
    return next;
}

Ref<SteamScreenshotRequest> SteamScreenshotRequest::tag_item(const String &published_id) const {
    auto next = branch();
    if (next->items.size() < maximum_tags) {
        next->items.append(published_id);
    } else if (next->draft_error == OK) {
        next->draft_error = ERR_INVALID_PARAMETER;
    }
    return next;
}

Variant SteamScreenshotRequest::submit() const { return send()->wait(); }

Ref<SteamScreenshotOperation> SteamScreenshotRequest::send() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto service = find_screenshots(owner);
        if (service.is_valid()) {
            return service->send(*this);
        }
    }
#endif
    Ref<SteamScreenshotOperation> operation;
    operation.instantiate();
    operation->finish(ERR_UNAVAILABLE, "The Steam screenshot scope is unavailable.");
    return operation;
}

void SteamScreenshots::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamScreenshots::is_available);
    ClassDB::bind_method(D_METHOD("capture"), &SteamScreenshots::capture);
    ClassDB::bind_method(D_METHOD("from_image", "image"), &SteamScreenshots::from_image);
    ADD_SIGNAL(MethodInfo("screenshot_ready", PropertyInfo(Variant::OBJECT, "screenshot", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamScreenshot")));
}

void SteamScreenshots::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || subscription || !scope || !scope->active || !get_screenshots(active)) {
        return;
    }
    const ObjectID id(get_instance_id());
    subscription = active->subscribe(ScreenshotReady_t::k_iCallback, [id](const void *record, size_t size) {
        const auto service = find_screenshots(id);
        if (service.is_valid() && record && size == sizeof(ScreenshotReady_t)) {
            ScreenshotReady_t ready{};
            std::memcpy(&ready, record, sizeof(ready));
            service->on_ready(ready.m_hLocal, ready.m_eResult == k_EResultOK);
        }
    });
    if (subscription) {
        runtime = active;
        client_scope = scope;
    }
#endif
}

void SteamScreenshots::detach() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(subscription);
    }
    subscription = 0;
    runtime.reset();
    client_scope.reset();
    std::map<uint32_t, Pending> closing;
    closing.swap(pending);
    for (const auto &entry : closing) {
        entry.second.operation->finish(ERR_UNAVAILABLE, "The Steam screenshot scope stopped.");
    }
#endif
}

void SteamScreenshots::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (!active_runtime() || pending.empty()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<Ref<SteamScreenshotOperation>> expired;
    for (auto entry = pending.begin(); entry != pending.end();) {
        if (entry->second.deadline <= now) {
            expired.push_back(entry->second.operation);
            entry = pending.erase(entry);
        } else {
            ++entry;
        }
    }
    for (const auto &operation : expired) {
        operation->finish(ERR_TIMEOUT, "Steam did not confirm the screenshot before the observation deadline.");
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamScreenshots::active_runtime() const {
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

bool SteamScreenshots::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return get_screenshots(active_runtime()) != nullptr;
#else
    return false;
#endif
}

Error SteamScreenshots::capture() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_screenshots(active);
    if (!interface) {
        return ERR_UNAVAILABLE;
    }
    const auto ready = active->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
            "SteamAPI_ISteamUtils_IsOverlayEnabled");
    const auto hooked = active->symbol<decltype(&SteamAPI_ISteamScreenshots_IsScreenshotsHooked)>(
            "SteamAPI_ISteamScreenshots_IsScreenshotsHooked");
    const auto trigger = active->symbol<decltype(&SteamAPI_ISteamScreenshots_TriggerScreenshot)>(
            "SteamAPI_ISteamScreenshots_TriggerScreenshot");
    if (!ready || !hooked || !trigger || !active->get_utils()) {
        return ERR_UNAVAILABLE;
    }
    if (!ready(active->get_utils()) || hooked(interface)) {
        return ERR_BUSY;
    }
    trigger(interface);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamScreenshotRequest> SteamScreenshots::from_image(const Ref<Image> &image) {
    Ref<SteamScreenshotRequest> draft;
    draft.instantiate();
    draft->owner = ObjectID(get_instance_id());
    if (!is_available()) {
        draft->draft_error = ERR_UNAVAILABLE;
        return draft;
    }
    if (image.is_null() || image->is_empty() || image->is_compressed()) {
        draft->draft_error = ERR_INVALID_PARAMETER;
        return draft;
    }
    const int32_t width = image->get_width();
    const int32_t height = image->get_height();
    if (width <= 0 || height <= 0 || width > maximum_dimension || height > maximum_dimension ||
            int64_t(width) * height > maximum_pixels) {
        draft->draft_error = ERR_INVALID_PARAMETER;
        return draft;
    }
    const Ref<Image> snapshot = image->duplicate();
    if (snapshot.is_null()) {
        draft->draft_error = ERR_CANT_CREATE;
        return draft;
    }
    snapshot->clear_mipmaps();
    snapshot->convert(Image::FORMAT_RGB8);
    if (snapshot->get_format() != Image::FORMAT_RGB8 || snapshot->get_data_size() != int64_t(width) * height * 3) {
        draft->draft_error = ERR_INVALID_DATA;
        return draft;
    }
    draft->size = Vector2i(width, height);
    draft->pixels = snapshot->get_data();
    return draft;
}

Ref<SteamScreenshotOperation> SteamScreenshots::send(const SteamScreenshotRequest &draft) {
    Ref<SteamScreenshotOperation> operation;
    operation.instantiate();
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_screenshots(active);
    if (interface) {
        if (draft.draft_error != OK || draft.pixels.is_empty()) {
            operation->finish(draft.draft_error != OK ? draft.draft_error : ERR_INVALID_PARAMETER,
                    "The screenshot needs a bounded, uncompressed image and at most 32 tags of each kind.");
            return operation;
        }
        if (pending.size() >= maximum_pending) {
            operation->finish(ERR_BUSY, "Too many Steam screenshots are awaiting confirmation.");
            return operation;
        }
        Pending entry;
        entry.location = draft.location.utf8();
        const int64_t location_size = entry.location.length();
        if (location_size > k_cubUFSTagValueMax ||
                (location_size > 0 && std::memchr(entry.location.get_data(), 0, static_cast<size_t>(location_size))) ||
                !read_tags(draft.users, true, entry.users) || !read_tags(draft.items, false, entry.items)) {
            operation->finish(ERR_INVALID_PARAMETER, "The screenshot location or a tag identifier is invalid.");
            return operation;
        }
        const auto write = active->symbol<decltype(&SteamAPI_ISteamScreenshots_WriteScreenshot)>(
                "SteamAPI_ISteamScreenshots_WriteScreenshot");
        if (write) {
            PackedByteArray bytes = draft.pixels;
            const ScreenshotHandle handle = write(interface, bytes.ptrw(), static_cast<uint32_t>(bytes.size()),
                    draft.size.x, draft.size.y);
            if (handle == INVALID_SCREENSHOT_HANDLE || pending.find(handle) != pending.end()) {
                operation->finish(ERR_CANT_CREATE, "Steam did not return a unique screenshot handle.");
                return operation;
            }
            operation->owner = ObjectID(get_instance_id());
            operation->handle = handle;
            entry.operation = operation;
            entry.deadline = Time::get_singleton()->get_ticks_msec() + observation_timeout_msec;
            pending.emplace(handle, std::move(entry));
            return operation;
        }
    }
#endif
    operation->finish(ERR_UNAVAILABLE, "The Steam screenshot scope is unavailable.");
    return operation;
}

Error SteamScreenshots::cancel(uint32_t handle) {
    const auto entry = pending.find(handle);
    if (entry == pending.end()) {
        return ERR_DOES_NOT_EXIST;
    }
    const auto operation = entry->second.operation;
    pending.erase(entry);
    operation->finish(ERR_SKIP, "Screenshot observation was cancelled locally.");
    return OK;
}

void SteamScreenshots::on_ready(uint32_t handle, bool saved) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_screenshots(active);
    if (!interface || handle == INVALID_SCREENSHOT_HANDLE) {
        return;
    }
    Pending request;
    const auto entry = pending.find(handle);
    if (entry != pending.end()) {
        request = std::move(entry->second);
        pending.erase(entry);
        if (request.deadline <= Time::get_singleton()->get_ticks_msec()) {
            const auto expired = std::exchange(request.operation, {});
            expired->finish(ERR_TIMEOUT, "Steam did not confirm the screenshot before the observation deadline.");
        }
    }
    if (!saved) {
        if (request.operation.is_valid()) {
            request.operation->finish(ERR_CANT_CREATE, "Steam could not save the screenshot.");
        }
        return;
    }
    Ref<SteamScreenshot> screenshot;
    screenshot.instantiate();
    if (request.operation.is_valid()) {
        const auto set_location = active->symbol<decltype(&SteamAPI_ISteamScreenshots_SetLocation)>(
                "SteamAPI_ISteamScreenshots_SetLocation");
        const auto tag_user = active->symbol<decltype(&SteamAPI_ISteamScreenshots_TagUser)>(
                "SteamAPI_ISteamScreenshots_TagUser");
        const auto tag_item = active->symbol<decltype(&SteamAPI_ISteamScreenshots_TagPublishedFile)>(
                "SteamAPI_ISteamScreenshots_TagPublishedFile");
        if (request.location.length() > 0 && (!set_location || !set_location(interface, handle, request.location.get_data()))) {
            screenshot->errors.append(set_location ? FAILED : ERR_UNAVAILABLE);
        }
        for (const uint64_t user : request.users) {
            if (!tag_user || !tag_user(interface, handle, user)) {
                screenshot->errors.append(tag_user ? FAILED : ERR_UNAVAILABLE);
            }
        }
        for (const uint64_t item : request.items) {
            if (!tag_item || !tag_item(interface, handle, item)) {
                screenshot->errors.append(tag_item ? FAILED : ERR_UNAVAILABLE);
            }
        }
    }
    screenshot->errors.make_read_only();
    if (request.operation.is_valid()) {
        request.operation->finish(OK, {}, screenshot);
    }
    if (active_runtime()) {
        emit_signal("screenshot_ready", screenshot);
    }
#endif
}

} // namespace crimson::steam
