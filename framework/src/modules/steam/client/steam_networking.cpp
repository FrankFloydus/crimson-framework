// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_networking.hpp"
#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Resolve the Steam Networking Utils interface from the same client module.
ISteamNetworkingUtils *relay_utils(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime->symbol<decltype(&SteamAPI_SteamNetworkingUtils_SteamAPI_v004)>(
            "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    return get ? get() : nullptr;
}

} // namespace

Ref<SteamConnectionPath> sample_steam_path(ISteamNetworkingSockets &sockets, uint32_t handle,
        int64_t token, uint64_t expected_account) {
    constexpr int insecure = k_nSteamNetworkConnectionInfoFlags_Unauthenticated |
            k_nSteamNetworkConnectionInfoFlags_Unencrypted;
    SteamNetConnectionInfo_t info{};
    if (!handle || !token || !expected_account || !sockets.GetConnectionInfo(handle, &info) ||
            info.m_eState != k_ESteamNetworkingConnectionState_Connected || info.m_nUserData != token ||
            (info.m_nFlags & insecure) || info.m_identityRemote.GetSteamID64() != expected_account) {
        return {};
    }
    auto route = SteamConnectionPath::UNKNOWN;
    if (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) {
        route = SteamConnectionPath::RELAY;
    } else if (info.m_addrRemote.m_port != 0 && !info.m_addrRemote.IsIPv6AllZeros()) {
        route = SteamConnectionPath::DIRECT;
    }
    SteamNetConnectionRealTimeStatus_t measured{};
    int32_t ping = -1;
    double loss = -1.0;
    if (sockets.GetConnectionRealTimeStatus(handle, &measured, 0, nullptr) == k_EResultOK &&
            measured.m_eState == k_ESteamNetworkingConnectionState_Connected) {
        ping = measured.m_nPing >= 0 ? measured.m_nPing : -1;
        const double quality = measured.m_flConnectionQualityLocal;
        if (std::isfinite(quality) && quality >= 0.0 && quality <= 1.0) {
            loss = 1.0 - quality;
        }
    }
    return SteamConnectionPath::snapshot(route, ping, loss);
}
#endif

void SteamConnectionPath::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_kind"), &SteamConnectionPath::get_kind);
    ClassDB::bind_method(D_METHOD("get_ping_ms"), &SteamConnectionPath::get_ping_ms);
    ClassDB::bind_method(D_METHOD("get_packet_loss"), &SteamConnectionPath::get_packet_loss);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ping_ms", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_ping_ms");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "packet_loss", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_packet_loss");
    BIND_ENUM_CONSTANT(UNKNOWN);
    BIND_ENUM_CONSTANT(DIRECT);
    BIND_ENUM_CONSTANT(RELAY);
}

SteamConnectionPath::RouteKind SteamConnectionPath::get_kind() const { return kind; }
int32_t SteamConnectionPath::get_ping_ms() const { return ping_ms; }
double SteamConnectionPath::get_packet_loss() const { return packet_loss; }

Ref<SteamConnectionPath> SteamConnectionPath::snapshot(RouteKind route, int32_t ping, double loss) {
    Ref<SteamConnectionPath> value;
    value.instantiate();
    value->kind = route;
    value->ping_ms = ping;
    value->packet_loss = loss;
    return value;
}

void SteamConnectionPathResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamConnectionPathResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamConnectionPath",
            PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamConnectionPath> SteamConnectionPathResult::get_value() const { return value; }

Ref<SteamConnectionPathResult> SteamConnectionPathResult::create(Error error, const Ref<SteamConnectionPath> &path) {
    Ref<SteamConnectionPathResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = path;
    return result;
}

void SteamRelayStatus::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_availability"), &SteamRelayStatus::get_availability);
    ClassDB::bind_method(D_METHOD("is_measuring"), &SteamRelayStatus::is_measuring);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "availability", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_availability");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "measuring", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_measuring");
    BIND_ENUM_CONSTANT(CANNOT_TRY);
    BIND_ENUM_CONSTANT(FAILED);
    BIND_ENUM_CONSTANT(PREVIOUSLY_AVAILABLE);
    BIND_ENUM_CONSTANT(RETRYING);
    BIND_ENUM_CONSTANT(UNKNOWN);
    BIND_ENUM_CONSTANT(NEVER_TRIED);
    BIND_ENUM_CONSTANT(WAITING);
    BIND_ENUM_CONSTANT(ATTEMPTING);
    BIND_ENUM_CONSTANT(CURRENT);
}

SteamRelayStatus::Availability SteamRelayStatus::get_availability() const { return availability; }
bool SteamRelayStatus::is_measuring() const { return measuring; }

Ref<SteamRelayStatus> SteamRelayStatus::snapshot(int state, bool measuring) {
    Ref<SteamRelayStatus> value;
    value.instantiate();
    switch (state) {
        case CANNOT_TRY:
        case FAILED:
        case PREVIOUSLY_AVAILABLE:
        case RETRYING:
        case NEVER_TRIED:
        case WAITING:
        case ATTEMPTING:
        case CURRENT:
            value->availability = static_cast<Availability>(state);
            break;
        default:
            break;
    }
    value->measuring = measuring;
    return value;
}

void SteamNetworking::_bind_methods() {
    ClassDB::bind_method(D_METHOD("relay_status"), &SteamNetworking::relay_status);
    ClassDB::bind_method(D_METHOD("warm_relay"), &SteamNetworking::warm_relay);
    ClassDB::bind_method(D_METHOD("peer_path", "peer_id"), &SteamNetworking::peer_path);
    ADD_SIGNAL(MethodInfo("relay_status_changed",
            PropertyInfo(Variant::OBJECT, "status", PROPERTY_HINT_RESOURCE_TYPE, "SteamRelayStatus")));
}

std::shared_ptr<SteamRuntime> SteamNetworking::live() const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return {};
    }
    const auto generation = scope.lock();
    return generation && generation->active ? runtime.lock() : nullptr;
#else
    return {};
#endif
}

void SteamNetworking::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &generation) {
#ifdef CRIMSON_WITH_STEAM
    runtime = active;
    scope = generation;
    const ObjectID self(get_instance_id());
    if (active) {
        subscription = active->subscribe(SteamRelayNetworkStatus_t::k_iCallback,
                [self](const void *data, size_t size) {
                    const Ref<SteamNetworking> service(
                            Object::cast_to<SteamNetworking>(ObjectDB::get_instance(self)));
                    if (service.is_null() || !service->live() || !data || size != sizeof(SteamRelayNetworkStatus_t)) {
                        return;
                    }
                    SteamRelayNetworkStatus_t status{};
                    std::memcpy(&status, data, sizeof(status));
                    service->emit_signal("relay_status_changed",
                            SteamRelayStatus::snapshot(status.m_eAvail, status.m_bPingMeasurementInProgress != 0));
                });
    }
#endif
}

void SteamNetworking::detach() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(subscription);
    }
#endif
    subscription = 0;
    runtime.reset();
    scope.reset();
    peer_source = {};
}

void SteamNetworking::set_game_peer_source(PeerSource source) {
    peer_source = std::move(source);
}

Ref<SteamRelayStatus> SteamNetworking::relay_status() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = live()) {
        const auto query = active->symbol<decltype(&SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus)>(
                "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus");
        auto *utils = relay_utils(active);
        if (query && utils) {
            SteamRelayNetworkStatus_t status{};
            const auto availability = query(utils, &status);
            return SteamRelayStatus::snapshot(availability, status.m_bPingMeasurementInProgress != 0);
        }
    }
#endif
    return SteamRelayStatus::snapshot(SteamRelayStatus::UNKNOWN, false);
}

Error SteamNetworking::warm_relay() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = live(); active && relay_utils(active) &&
            active->symbol<decltype(&SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess)>(
                    "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess")) {
        active->warm_relay();
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Ref<SteamConnectionPathResult> SteamNetworking::peer_path(int64_t peer_id) const {
    if (peer_id <= 0 || peer_id > (std::numeric_limits<int32_t>::max)()) {
        return SteamConnectionPathResult::create(ERR_INVALID_PARAMETER);
    }
    if (!live() || !peer_source) {
        return SteamConnectionPathResult::create(ERR_UNAVAILABLE);
    }
    const auto path = peer_source(static_cast<int32_t>(peer_id));
    return SteamConnectionPathResult::create(path.is_valid() ? OK : ERR_DOES_NOT_EXIST, path);
}

} // namespace crimson::steam
