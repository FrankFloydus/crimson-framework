// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/online_directory.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// Directory requests and callbacks share the Godot scene thread.
bool on_scene_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

} // namespace

void OnlineDirectoryRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("publish", "listing_id"), &OnlineDirectoryRequest::publish);
    ClassDB::bind_method(D_METHOD("resolve", "target"), &OnlineDirectoryRequest::resolve);
    ClassDB::bind_method(D_METHOD("fail"), &OnlineDirectoryRequest::fail);
    ClassDB::bind_method(D_METHOD("is_cancelled"), &OnlineDirectoryRequest::is_cancelled);
}

Error OnlineDirectoryRequest::publish(const String &listing_id) {
    if (!on_scene_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (kind != Kind::Publish || listing_id.is_empty() || listing_id.length() > 192 ||
            listing_id != listing_id.strip_edges()) {
        return ERR_INVALID_PARAMETER;
    }
    if (state == State::Cancelled) {
        // Consume the one eventual callback before invoking the trusted cleanup hook.
        state = State::CancelledSettled;
        const Ref<OnlineDirectory> directory(Object::cast_to<OnlineDirectory>(ObjectDB::get_instance(directory_id)));
        if (directory.is_null()) {
            return ERR_UNAVAILABLE;
        }
        directory->unpublish(listing_id);
        return OK;
    }
    if (state != State::Pending) {
        return ERR_ALREADY_IN_USE;
    }
    listing = listing_id;
    state = State::Finished;
    return OK;
}

Error OnlineDirectoryRequest::resolve(const Ref<session::SessionEndpoint> &target) {
    if (!on_scene_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (kind != Kind::Resolve || target.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    if (state != State::Pending) {
        return ERR_ALREADY_IN_USE;
    }
    endpoint = target;
    state = State::Finished;
    return OK;
}

Error OnlineDirectoryRequest::fail() {
    if (!on_scene_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (kind == Kind::Unconfigured || state != State::Pending) {
        return ERR_ALREADY_IN_USE;
    }
    state = State::Finished;
    return OK;
}

bool OnlineDirectoryRequest::is_cancelled() const {
    return on_scene_thread() && (state == State::Cancelled || state == State::CancelledSettled);
}

void OnlineDirectoryRequest::cancel() {
    if (state == State::Pending) {
        state = State::Cancelled;
        return;
    }
    if (state != State::Finished) {
        return;
    }
    state = State::CancelledSettled;
    const String retired_listing = take_listing();
    endpoint.unref();
    if (!retired_listing.is_empty()) {
        const Ref<OnlineDirectory> directory(Object::cast_to<OnlineDirectory>(ObjectDB::get_instance(directory_id)));
        if (directory.is_valid()) {
            directory->unpublish(retired_listing);
        }
    }
}

String OnlineDirectoryRequest::take_listing() {
    return std::exchange(listing, String());
}

void OnlineDirectory::_bind_methods() {
    GDVIRTUAL_BIND(_publish, "endpoint", "world_id", "request");
    GDVIRTUAL_BIND(_resolve, "listing_id", "request");
    GDVIRTUAL_BIND(_unpublish, "listing_id");
    GDVIRTUAL_BIND(_leave, "listing_id");
}

bool OnlineDirectory::is_game_available() const {
    return true;
}

bool OnlineDirectory::supports_host_mode(bool) const {
    return true;
}

void OnlineDirectory::retire_attachment() {}

bool OnlineDirectory::accepts_listing(const Ref<OnlineListing> &) const {
    return false;
}

Ref<OnlineDirectoryRequest> OnlineDirectory::publish(
        const Ref<session::SessionEndpoint> &endpoint, const StringName &world) {
    Ref<OnlineDirectoryRequest> request;
    request.instantiate();
    request->kind = OnlineDirectoryRequest::Kind::Publish;
    request->directory_id = ObjectID(get_instance_id());
    begin_publish(endpoint, world, request);
    return request;
}

Ref<OnlineDirectoryRequest> OnlineDirectory::resolve(const String &listing_id) {
    Ref<OnlineDirectoryRequest> request;
    request.instantiate();
    request->kind = OnlineDirectoryRequest::Kind::Resolve;
    begin_resolve(listing_id, request);
    return request;
}

void OnlineDirectory::unpublish(const String &listing_id) {
    remove_listing(listing_id);
}

void OnlineDirectory::begin_publish(const Ref<session::SessionEndpoint> &endpoint,
        const StringName &world, const Ref<OnlineDirectoryRequest> &request) {
    if (!GDVIRTUAL_IS_OVERRIDDEN(_unpublish) || !GDVIRTUAL_CALL(_publish, endpoint, world, request)) {
        request->fail();
    }
}

void OnlineDirectory::begin_resolve(const String &listing_id,
        const Ref<OnlineDirectoryRequest> &request) {
    if (!GDVIRTUAL_CALL(_resolve, listing_id, request)) {
        request->fail();
    }
}

void OnlineDirectory::remove_listing(const String &listing_id) {
    GDVIRTUAL_CALL(_unpublish, listing_id);
}

void OnlineDirectory::leave_listing(const String &listing_id) {
    GDVIRTUAL_CALL(_leave, listing_id);
}

} // namespace crimson::integration
