// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_relay_coordinator.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include <utility>

namespace crimson::integration {
using namespace godot;

namespace {

// Trusted backend completion still shares the native scene-thread lifecycle.
bool on_scene_thread() {
    const auto *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

} // namespace

void SteamRelayRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_kind"), &SteamRelayRequest::get_kind);
    ClassDB::bind_method(D_METHOD("get_profile"), &SteamRelayRequest::get_profile);
    ClassDB::bind_method(D_METHOD("get_listing_id"), &SteamRelayRequest::get_listing_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamRelayRequest::get_app_id);
    ClassDB::bind_method(D_METHOD("get_namespace"), &SteamRelayRequest::get_namespace);
    ClassDB::bind_method(D_METHOD("get_server_steam_id"), &SteamRelayRequest::get_server_steam_id);
    ClassDB::bind_method(D_METHOD("get_virtual_port"), &SteamRelayRequest::get_virtual_port);
    ClassDB::bind_method(D_METHOD("get_registration_id"), &SteamRelayRequest::get_registration_id);
    ClassDB::bind_method(D_METHOD("get_world_id"), &SteamRelayRequest::get_world_id);
    ClassDB::bind_method(D_METHOD("get_login_blob"), &SteamRelayRequest::get_login_blob);
    ClassDB::bind_method(D_METHOD("get_local_steam_id"), &SteamRelayRequest::get_local_steam_id);
    ClassDB::bind_method(D_METHOD("registered"), &SteamRelayRequest::registered);
    ClassDB::bind_method(D_METHOD("authorize", "ticket"), &SteamRelayRequest::authorize);
    ClassDB::bind_method(D_METHOD("fail"), &SteamRelayRequest::fail);
    ClassDB::bind_method(D_METHOD("is_cancelled"), &SteamRelayRequest::is_cancelled);
    BIND_ENUM_CONSTANT(UNCONFIGURED);
    BIND_ENUM_CONSTANT(REGISTER);
    BIND_ENUM_CONSTANT(AUTHORIZE);
}

SteamRelayRequest::Kind SteamRelayRequest::get_kind() const { return kind; }
StringName SteamRelayRequest::get_profile() const { return route.profile; }
String SteamRelayRequest::get_listing_id() const { return listing_id; }
int64_t SteamRelayRequest::get_app_id() const { return app_id; }
StringName SteamRelayRequest::get_namespace() const { return name_space; }
String SteamRelayRequest::get_server_steam_id() const { return route.server_id ? String::num_uint64(route.server_id) : String(); }
int32_t SteamRelayRequest::get_virtual_port() const { return route.virtual_port; }
String SteamRelayRequest::get_registration_id() const { return route.registration ? String::num_uint64(route.registration) : String(); }
StringName SteamRelayRequest::get_world_id() const { return world_id; }
PackedByteArray SteamRelayRequest::get_login_blob() const {
    return on_scene_thread() ? login_blob.duplicate() : PackedByteArray();
}
String SteamRelayRequest::get_local_steam_id() const { return local_steam_id ? String::num_uint64(local_steam_id) : String(); }

Ref<SteamRelayRequest> SteamRelayRequest::create(Kind selected, const steam::SteamHostedRoute &target,
        uint32_t app, const StringName &scope, const SteamRelayCoordinator &coordinator) {
    const String id = target.listing_id(app, scope);
    if (selected == UNCONFIGURED || id.is_empty()) {
        return {};
    }
    Ref<SteamRelayRequest> request;
    request.instantiate();
    request->kind = selected;
    request->route = target;
    request->app_id = app;
    request->name_space = scope;
    request->listing_id = id;
    request->coordinator_id = ObjectID(coordinator.get_instance_id());
    return request;
}

void SteamRelayRequest::set_server_login(const StringName &world, const PackedByteArray &blob) {
    world_id = world;
    login_blob = blob;
}

void SteamRelayRequest::set_local_account(uint64_t account) { local_steam_id = account; }

Error SteamRelayRequest::registered() {
    if (!on_scene_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (kind != REGISTER) {
        return ERR_INVALID_PARAMETER;
    }
    if (phase == Phase::CANCELLED) {
        const Ref<SteamRelayRequest> keep_alive(this);
        phase = Phase::CANCELLED_SETTLED;
        return compensate();
    }
    if (phase != Phase::PENDING) {
        return ERR_ALREADY_IN_USE;
    }
    login_blob.clear();
    phase = Phase::REGISTERED;
    return OK;
}

Error SteamRelayRequest::authorize(const PackedByteArray &ticket) {
    if (!on_scene_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (kind != AUTHORIZE || ticket.is_empty() || ticket.size() > 4096) {
        return ERR_INVALID_PARAMETER;
    }
    if (phase != Phase::PENDING) {
        return ERR_ALREADY_IN_USE;
    }
    relay_ticket = ticket.duplicate();
    phase = Phase::AUTHORIZED;
    return OK;
}

Error SteamRelayRequest::fail() {
    if (!on_scene_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (kind == UNCONFIGURED || phase != Phase::PENDING) {
        return ERR_ALREADY_IN_USE;
    }
    phase = Phase::FAILED;
    login_blob.clear();
    return OK;
}

bool SteamRelayRequest::is_cancelled() const {
    return on_scene_thread() && (phase == Phase::CANCELLED || phase == Phase::CANCELLED_SETTLED);
}

bool SteamRelayRequest::is_finished() const { return phase != Phase::PENDING; }

bool SteamRelayRequest::take_registration() {
    if (phase != Phase::REGISTERED) {
        return false;
    }
    phase = Phase::CONSUMED;
    return true;
}

PackedByteArray SteamRelayRequest::take_ticket() {
    if (phase != Phase::AUTHORIZED) {
        return {};
    }
    phase = Phase::CONSUMED;
    return std::exchange(relay_ticket, {});
}

void SteamRelayRequest::cancel() {
    login_blob.clear();
    relay_ticket.clear();
    if (phase == Phase::PENDING) {
        phase = Phase::CANCELLED;
    } else if (phase == Phase::REGISTERED) {
        phase = Phase::CANCELLED_SETTLED;
        compensate();
    } else if (phase == Phase::AUTHORIZED) {
        phase = Phase::CANCELLED_SETTLED;
    }
}

Error SteamRelayRequest::compensate() {
    const Ref<SteamRelayCoordinator> coordinator(Object::cast_to<SteamRelayCoordinator>(ObjectDB::get_instance(coordinator_id)));
    if (coordinator.is_null()) {
        return ERR_UNAVAILABLE;
    }
    coordinator->withdraw(listing_id);
    return OK;
}

void SteamRelayCoordinator::_bind_methods() {
    GDVIRTUAL_BIND(_register_server, "request");
    GDVIRTUAL_BIND(_authorize_client, "request");
    GDVIRTUAL_BIND(_withdraw_server, "listing_id");
}

bool SteamRelayCoordinator::supports_host() {
    return GDVIRTUAL_IS_OVERRIDDEN(_register_server) && GDVIRTUAL_IS_OVERRIDDEN(_withdraw_server);
}

bool SteamRelayCoordinator::supports_client() {
    return GDVIRTUAL_IS_OVERRIDDEN(_authorize_client);
}

void SteamRelayCoordinator::register_server(const Ref<SteamRelayRequest> &request) {
    if (!supports_host() || !GDVIRTUAL_CALL(_register_server, request)) {
        request->fail();
    }
}

void SteamRelayCoordinator::authorize_client(const Ref<SteamRelayRequest> &request) {
    if (!supports_client() || !GDVIRTUAL_CALL(_authorize_client, request)) {
        request->fail();
    }
}

void SteamRelayCoordinator::withdraw(const String &listing_id) {
    GDVIRTUAL_CALL(_withdraw_server, listing_id);
}

} // namespace crimson::integration
