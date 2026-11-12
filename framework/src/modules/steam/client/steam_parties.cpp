// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_parties.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Stable opaque prefix understood by the game directory.
constexpr const char *party_route_prefix = "party1";

#ifdef CRIMSON_WITH_STEAM
// Limit native reads and simultaneous reservations from externally supplied beacon data.
constexpr uint32_t maximum_locations = 128;
constexpr uint32_t maximum_visible_beacons = 128;
constexpr int32_t maximum_open_slots = 64;
// Local observation and abandoned seat deadlines.
constexpr uint64_t observation_ms = 15000;
constexpr uint64_t reservation_ms = 30000;
constexpr uint64_t host_tick_ms = 100;
// Extra attempts to remove a beacon are deliberately slower than the world tick.
constexpr uint64_t cleanup_retry_ms = 1000;

// A single process-wide SDK beacon. Keep pending calls reserved until their exact results drain.
struct PartyGate {
    // Native process generation containing the owned Steam beacon.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact scoped facade allowed to operate on the native host beacon.
    ObjectID host;
    // Outstanding native publication, including an abandoned observer.
    SteamAPICall_t publishing = k_uAPICallInvalid;
    // Outstanding native guest reservation, including an abandoned observer.
    SteamAPICall_t reserving = k_uAPICallInvalid;
    // Native host beacon still needing retirement when the local owner disappears.
    uint64_t beacon = 0;
    // Do not call DestroyBeacon every frame if the backend temporarily refuses.
    uint64_t next_cleanup = 0;
};

// All Steam Parties calls occur on the Godot main thread.
PartyGate process_party;

// Resolve Parties from this exact client runtime. Dedicated authorities have no client interface.
ISteamParties *party_api(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamParties_v002)>("SteamAPI_SteamParties_v002") :
                               nullptr;
    return get ? get() : nullptr;
}

// Reset transient SDK IDs only when Steam actually started a new generation.
PartyGate &party_gate(const std::shared_ptr<SteamRuntime> &runtime) {
    if (process_party.runtime.lock() != runtime) {
        process_party = {};
        process_party.runtime = runtime;
    }
    return process_party;
}

// Keep the scoped service alive during one callback without keeping its SDK pipe alive.
Ref<SteamParties> party_service(ObjectID id) {
    return Ref<SteamParties>(Object::cast_to<SteamParties>(ObjectDB::get_instance(id)));
}

// Return a Godot status for one exact Steam async result.
Error party_error(EResult status) {
    switch (status) {
        case k_EResultOK: return OK;
        case k_EResultAccessDenied: return ERR_UNAUTHORIZED;
        case k_EResultInvalidParam: return ERR_INVALID_PARAMETER;
        case k_EResultLimitExceeded:
        case k_EResultBusy: return ERR_BUSY;
        case k_EResultNoConnection: return ERR_CONNECTION_ERROR;
        case k_EResultServiceUnavailable: return ERR_UNAVAILABLE;
        case k_EResultTimeout: return ERR_TIMEOUT;
        default: return FAILED;
    }
}

// Use Steam's ordinary numeric lobby launch argument for native entry points.
String connect_text(uint64_t lobby) {
    return String("+connect_lobby ") + String::num_uint64(lobby);
}

// Bound native text to a fixed callback or SDK buffer before importing it.
String bounded_text(const char *bytes, size_t capacity) {
    const auto *end = static_cast<const char *>(std::memchr(bytes, '\0', capacity));
    return end ? String::utf8(bytes, end - bytes) : String();
}

// Destroy the exact locally created beacon. Preserve a native cleanup obligation on failure.
bool destroy_beacon(const std::shared_ptr<SteamRuntime> &runtime, uint64_t id) {
    ISteamParties *parties = party_api(runtime);
    const auto destroy = parties ? runtime->symbol<decltype(&SteamAPI_ISteamParties_DestroyBeacon)>(
                                         "SteamAPI_ISteamParties_DestroyBeacon") : nullptr;
    auto &gate = party_gate(runtime);
    const bool destroyed = destroy && destroy(parties, id);
    if (destroyed && gate.beacon == id) {
        gate.beacon = 0;
        gate.host = ObjectID();
    } else if (!destroyed && (gate.beacon == 0 || gate.beacon == id)) {
        gate.beacon = id;
        gate.host = ObjectID();
        gate.next_cleanup = Time::get_singleton()->get_ticks_msec() + cleanup_retry_ms;
    }
    return destroyed;
}

// Prove a discovered location belongs to the supported Steam group family.
bool valid_location(const SteamPartyBeaconLocation_t &location) {
    return location.m_eType == k_ESteamPartyBeaconLocationType_ChatGroup && location.m_ulLocationID != 0;
}

// Count only reservations not already represented by an admitted gameplay seat.
int32_t remaining_reservations(const std::map<uint64_t, Ref<SteamPartyReservation>> &reservations,
        const PackedStringArray &admitted) {
    int32_t count = 0;
    for (const auto &[account, reservation] : reservations) {
        if (!admitted.has(reservation->get_steam_id())) {
            ++count;
        }
    }
    return count;
}
#endif

} // namespace

bool parse_steam_party_route(const String &route, uint64_t &lobby, uint64_t &owner) {
    if (route.length() > 48 || !route.begins_with("party1:")) {
        return false;
    }
    const PackedStringArray fields = route.split(":");
    return fields.size() == 3 && fields[0] == party_route_prefix &&
            parse_steam_lobby_id(fields[1], lobby) && parse_steam_lobby_account(fields[2], owner);
}

void SteamPartyLocation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &SteamPartyLocation::get_id);
    ClassDB::bind_method(D_METHOD("get_kind"), &SteamPartyLocation::get_kind);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_kind");
    BIND_ENUM_CONSTANT(CHAT_GROUP);
}

String SteamPartyLocation::get_id() const { return id ? String::num_uint64(id) : String(); }
SteamPartyLocation::Kind SteamPartyLocation::get_kind() const { return CHAT_GROUP; }

void SteamPartyListing::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &SteamPartyListing::get_id);
    ClassDB::bind_method(D_METHOD("get_owner_steam_id"), &SteamPartyListing::get_owner_steam_id);
    ClassDB::bind_method(D_METHOD("get_location"), &SteamPartyListing::get_location);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "owner_steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_owner_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "location", PROPERTY_HINT_RESOURCE_TYPE, "SteamPartyLocation",
                         PROPERTY_USAGE_READ_ONLY), "", "get_location");
}

String SteamPartyListing::get_id() const { return id ? String::num_uint64(id) : String(); }
String SteamPartyListing::get_owner_steam_id() const { return owner ? String::num_uint64(owner) : String(); }
Ref<SteamPartyLocation> SteamPartyListing::get_location() const { return location; }

void SteamPartyReservation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamPartyReservation::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_beacon_id"), &SteamPartyReservation::get_beacon_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "beacon_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_beacon_id");
}

String SteamPartyReservation::get_steam_id() const { return account ? String::num_uint64(account) : String(); }
String SteamPartyReservation::get_beacon_id() const { return beacon ? String::num_uint64(beacon) : String(); }

void SteamPartyLocationsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPartyLocationsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, "SteamPartyLocation",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

TypedArray<SteamPartyLocation> SteamPartyLocationsResult::get_value() const { return value; }

Ref<SteamPartyLocationsResult> SteamPartyLocationsResult::create(Error error,
        const TypedArray<SteamPartyLocation> &locations) {
    Ref<SteamPartyLocationsResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value.assign(locations);
    result->value.make_read_only();
    return result;
}

void SteamPartyListingsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPartyListingsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, "SteamPartyListing",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

TypedArray<SteamPartyListing> SteamPartyListingsResult::get_value() const { return value; }

Ref<SteamPartyListingsResult> SteamPartyListingsResult::create(Error error,
        const TypedArray<SteamPartyListing> &listings) {
    Ref<SteamPartyListingsResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value.assign(listings);
    result->value.make_read_only();
    return result;
}

void SteamPartyBeaconResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPartyBeaconResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamPartyBeacon",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamPartyBeacon> SteamPartyBeaconResult::get_value() const { return value; }

Ref<SteamPartyBeaconResult> SteamPartyBeaconResult::create(Error error, const Ref<SteamPartyBeacon> &beacon) {
    Ref<SteamPartyBeaconResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = beacon;
    return result;
}

void SteamPartySlotsOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamPartySlotsOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamPartySlotsOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamPartySlotsOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamPartySlotsOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamBoolResult")));
}

bool SteamPartySlotsOperation::is_finished() const { return outcome.is_valid(); }
Ref<SteamBoolResult> SteamPartySlotsOperation::result() const { return outcome; }
Variant SteamPartySlotsOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamPartySlotsOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
#endif
    if (outcome.is_valid()) {
        return ERR_ALREADY_IN_USE;
    }
    finish(ERR_SKIP);
    return OK;
}

void SteamPartySlotsOperation::finish(Error error) {
    if (outcome.is_null()) {
        outcome = SteamBoolResult::create(error, error == OK);
        emit_signal("completed", outcome);
    }
}

void SteamPartyBeacon::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &SteamPartyBeacon::get_id);
    ClassDB::bind_method(D_METHOD("change_open_slots", "slots"), &SteamPartyBeacon::change_open_slots);
    ClassDB::bind_method(D_METHOD("complete", "reservation"), &SteamPartyBeacon::complete);
    ClassDB::bind_method(D_METHOD("cancel", "reservation"), &SteamPartyBeacon::cancel);
    ClassDB::bind_method(D_METHOD("close"), &SteamPartyBeacon::close);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_id");
    ADD_SIGNAL(MethodInfo("reservation_received", PropertyInfo(Variant::OBJECT, "reservation",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamPartyReservation")));
    ADD_SIGNAL(MethodInfo("closed", PropertyInfo(Variant::INT, "reason")));
}

String SteamPartyBeacon::get_id() const { return id ? String::num_uint64(id) : String(); }

Ref<SteamParties> SteamPartyBeacon::service() const {
#ifdef CRIMSON_WITH_STEAM
    return SteamRuntime::on_main_thread() ? party_service(service_id) : Ref<SteamParties>();
#else
    return {};
#endif
}

Ref<SteamPartySlotsOperation> SteamPartyBeacon::change_open_slots(int64_t slots) {
    if (const auto owner = service(); owner.is_valid()) {
        return owner->set_open_slots(this, slots);
    }
    Ref<SteamPartySlotsOperation> operation;
    operation.instantiate();
    operation->finish(ERR_UNAVAILABLE);
    return operation;
}

Error SteamPartyBeacon::complete(const Ref<SteamPartyReservation> &reservation) {
    const auto owner = service();
    return owner.is_valid() ? owner->finish_reservation(this, reservation, true) : ERR_UNAVAILABLE;
}

Error SteamPartyBeacon::cancel(const Ref<SteamPartyReservation> &reservation) {
    const auto owner = service();
    return owner.is_valid() ? owner->finish_reservation(this, reservation, false) : ERR_UNAVAILABLE;
}

Error SteamPartyBeacon::close() {
    const auto owner = service();
    return owner.is_valid() ? owner->close_beacon(this, OK) : ERR_UNAVAILABLE;
}

void SteamPartyObservation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("publication_completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamPartyBeaconResult")));
    ADD_SIGNAL(MethodInfo("reservation_completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamListingResult")));
}

void SteamParties::_bind_methods() {
    ClassDB::bind_method(D_METHOD("locations"), &SteamParties::locations);
    ClassDB::bind_method(D_METHOD("beacons"), &SteamParties::beacons);
    ClassDB::bind_method(D_METHOD("publish", "listing", "location", "open_slots"), &SteamParties::publish);
    ClassDB::bind_method(D_METHOD("reserve", "beacon"), &SteamParties::reserve);
    ADD_SIGNAL(MethodInfo("beacons_changed"));
}

std::shared_ptr<SteamRuntime> SteamParties::active_runtime() const {
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

void SteamParties::set_game_source(std::function<SteamPartyHostState()> source) {
    game_source = std::move(source);
}

SteamPartyHostState SteamParties::host_state() const {
    return game_source ? game_source() : SteamPartyHostState{};
}

Ref<SteamPartyLocationsResult> SteamParties::locations() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamParties *parties = party_api(active);
    if (parties) {
        const auto count_locations = active->symbol<decltype(&SteamAPI_ISteamParties_GetNumAvailableBeaconLocations)>(
                "SteamAPI_ISteamParties_GetNumAvailableBeaconLocations");
        const auto get_locations = active->symbol<decltype(&SteamAPI_ISteamParties_GetAvailableBeaconLocations)>(
                "SteamAPI_ISteamParties_GetAvailableBeaconLocations");
        if (count_locations && get_locations) {
            uint32_t count = 0;
            if (!count_locations(parties, &count) || count > maximum_locations) {
                return SteamPartyLocationsResult::create(count > maximum_locations ? ERR_INVALID_DATA : FAILED);
            }
            std::array<SteamPartyBeaconLocation_t, maximum_locations> native{};
            if (count != 0 && !get_locations(parties, native.data(), count)) {
                return SteamPartyLocationsResult::create(FAILED);
            }
            TypedArray<SteamPartyLocation> locations;
            std::set<uint64_t> seen;
            for (uint32_t index = 0; index < count; ++index) {
                if (valid_location(native[index]) && seen.insert(native[index].m_ulLocationID).second) {
                    Ref<SteamPartyLocation> location;
                    location.instantiate();
                    location->id = native[index].m_ulLocationID;
                    locations.append(location);
                }
            }
            return SteamPartyLocationsResult::create(OK, locations);
        }
    }
#endif
    return SteamPartyLocationsResult::create(ERR_UNAVAILABLE);
}

Ref<SteamPartyListing> SteamParties::read_beacon(const std::shared_ptr<SteamRuntime> &active, uint64_t id) const {
#ifdef CRIMSON_WITH_STEAM
    ISteamParties *parties = party_api(active);
    const auto details = parties ? active->symbol<decltype(&SteamAPI_ISteamParties_GetBeaconDetails)>(
                                          "SteamAPI_ISteamParties_GetBeaconDetails") : nullptr;
    if (!details || id == 0) {
        return {};
    }
    CSteamID owner;
    SteamPartyBeaconLocation_t location{};
    std::array<char, 256> metadata{};
    if (!details(parties, id, &owner, &location, metadata.data(), static_cast<int>(metadata.size())) ||
            !owner.IsValid() || !owner.BIndividualAccount() || owner.ConvertToUint64() == active->get_user_id() ||
            !valid_location(location)) {
        return {};
    }
    const PackedStringArray fields = bounded_text(metadata.data(), metadata.size()).split(":");
    uint64_t lobby = 0;
    if (fields.size() != 4 || fields[0] != party_route_prefix ||
            fields[1] != String::num_uint64(active->get_app_id()) || !valid_steam_lobby_namespace(fields[2]) ||
            !parse_steam_lobby_id(fields[3], lobby)) {
        return {};
    }
    Ref<SteamPartyListing> listing;
    listing.instantiate();
    listing->id = id;
    listing->owner = owner.ConvertToUint64();
    listing->lobby = lobby;
    listing->app_id = active->get_app_id();
    listing->name_space = StringName(fields[2]);
    listing->location.instantiate();
    listing->location->id = location.m_ulLocationID;
    return listing;
#else
    return {};
#endif
}

Ref<SteamPartyListingsResult> SteamParties::beacons() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamParties *parties = party_api(active);
    if (parties) {
        const auto count_beacons = active->symbol<decltype(&SteamAPI_ISteamParties_GetNumActiveBeacons)>(
                "SteamAPI_ISteamParties_GetNumActiveBeacons");
        const auto beacon_at = active->symbol<decltype(&SteamAPI_ISteamParties_GetBeaconByIndex)>(
                "SteamAPI_ISteamParties_GetBeaconByIndex");
        if (count_beacons && beacon_at) {
            const uint32_t count = (std::min)(count_beacons(parties), maximum_visible_beacons);
            TypedArray<SteamPartyListing> listings;
            std::set<uint64_t> seen;
            for (uint32_t index = 0; index < count; ++index) {
                const uint64_t id = beacon_at(parties, index);
                if (seen.insert(id).second) {
                    if (const auto listing = read_beacon(active, id); listing.is_valid()) {
                        listings.append(listing);
                    }
                }
            }
            return SteamPartyListingsResult::create(OK, listings);
        }
    }
#endif
    return SteamPartyListingsResult::create(ERR_UNAVAILABLE);
}

void SteamParties::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
    runtime = active;
    client_scope = scope;
#ifdef CRIMSON_WITH_STEAM
    if (!party_api(active)) {
        return;
    }
    const ObjectID self(get_instance_id());
    reservation_subscription = active->subscribe(ReservationNotificationCallback_t::k_iCallback,
            [self](const void *data, size_t size) {
                if (const auto service = party_service(self); service.is_valid()) {
                    service->on_reservation(data, size);
                }
            });
    // Steam uses empty invalidation records. All details are read fresh by the next query.
    const auto changed = [self](const void *, size_t) {
        if (const auto service = party_service(self); service.is_valid() && service->active_runtime()) {
            service->emit_signal("beacons_changed");
        }
    };
    beacons_subscription = active->subscribe(ActiveBeaconsUpdated_t::k_iCallback, changed);
    locations_subscription = active->subscribe(AvailableBeaconLocationsUpdated_t::k_iCallback, changed);
#endif
}

void SteamParties::detach() {
    client_scope.reset();
    game_source = {};
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(std::exchange(reservation_subscription, 0));
        active->unsubscribe(std::exchange(beacons_subscription, 0));
        active->unsubscribe(std::exchange(locations_subscription, 0));
    }
#endif
    retire_game_beacon();
    runtime.reset();
    if (const auto observer = std::exchange(reserving, {}); observer.is_valid()) {
        observer->emit_signal("reservation_completed", SteamListingResult::from_listing(ERR_SKIP));
    }
}

Variant SteamParties::publish(const Ref<SteamListing> &listing,
        const Ref<SteamPartyLocation> &location, int64_t open_slots) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamParties *parties = party_api(active);
    if (!parties) {
        return SteamPartyBeaconResult::create(ERR_UNAVAILABLE);
    }
    if (listing.is_null() || location.is_null() || location->id == 0 || listing->is_social() ||
            listing->get_kind() != SteamListing::LOBBY || listing->get_app_id() != active->get_app_id() ||
            open_slots < 1 || open_slots > maximum_open_slots) {
        return SteamPartyBeaconResult::create(ERR_INVALID_PARAMETER);
    }
    const auto state = host_state();
    uint64_t lobby = 0;
    if (!state.joinable || state.lobby_id != listing->get_id() || state.name_space != listing->get_namespace() ||
            !parse_steam_lobby_id(state.lobby_id, lobby) || open_slots > state.free_slots) {
        return SteamPartyBeaconResult::create(ERR_UNAUTHORIZED);
    }
    auto &gate = party_gate(active);
    if (gate.publishing != k_uAPICallInvalid || gate.beacon || !active->can_track_call()) {
        return SteamPartyBeaconResult::create(ERR_BUSY);
    }
    const auto fresh_locations = locations();
    if (!fresh_locations->is_ok()) {
        return SteamPartyBeaconResult::create(fresh_locations->get_error());
    }
    bool eligible = false;
    const auto options = fresh_locations->get_value();
    for (int64_t i = 0; i < options.size(); ++i) {
        const Ref<SteamPartyLocation> candidate = options[i];
        if (candidate->id == location->id) {
            eligible = true;
            break;
        }
    }
    if (!eligible) {
        return SteamPartyBeaconResult::create(ERR_UNAUTHORIZED);
    }
    const auto create = active->symbol<decltype(&SteamAPI_ISteamParties_CreateBeacon)>(
            "SteamAPI_ISteamParties_CreateBeacon");
    const auto destroy = active->symbol<decltype(&SteamAPI_ISteamParties_DestroyBeacon)>(
            "SteamAPI_ISteamParties_DestroyBeacon");
    if (!create || !destroy || !reservation_subscription) {
        return SteamPartyBeaconResult::create(ERR_UNAVAILABLE);
    }
    SteamPartyBeaconLocation_t native{ k_ESteamPartyBeaconLocationType_ChatGroup, location->id };
    const CharString connect = connect_text(lobby).utf8();
    const CharString metadata = (String(party_route_prefix) + ":" + String::num_uint64(active->get_app_id()) +
            ":" + String(state.name_space) + ":" + state.lobby_id).utf8();
    const SteamAPICall_t call = create(parties, static_cast<uint32_t>(open_slots), &native,
            connect.get_data(), metadata.get_data());
    if (call == k_uAPICallInvalid) {
        return SteamPartyBeaconResult::create(FAILED);
    }
    gate.publishing = call;
    const ObjectID self(get_instance_id());
    const std::weak_ptr<SteamRuntime> weak(active);
    const auto lobby_id = state.lobby_id;
    const int32_t slot_count = static_cast<int32_t>(open_slots);
    const int32_t seat_ceiling = static_cast<int32_t>(state.admitted.size()) + slot_count;
    if (!active->watch_call(call, CreateBeaconCallback_t::k_iCallback, sizeof(CreateBeaconCallback_t),
                [self, weak, call, lobby_id, slot_count, seat_ceiling](const void *data) {
                    const auto runtime = weak.lock();
                    if (!runtime) {
                        return;
                    }
                    auto &gate = party_gate(runtime);
                    if (gate.publishing != call) {
                        return;
                    }
                    gate.publishing = k_uAPICallInvalid;
                    CreateBeaconCallback_t event{};
                    if (data) {
                        std::memcpy(&event, data, sizeof(event));
                    }
                    Error status = data ? party_error(event.m_eResult) : FAILED;
                    const auto service = party_service(self);
                    const auto observer = service.is_valid() ? std::exchange(service->publishing, {}) :
                                                               Ref<SteamPartyObservation>();
                    Ref<SteamPartyBeacon> beacon;
                    if (status == OK) {
                        const auto state = service.is_valid() ? service->host_state() : SteamPartyHostState{};
                        if (!event.m_ulBeaconID) {
                            status = ERR_INVALID_DATA;
                        } else if (observer.is_null() || !service->active_runtime() ||
                                !state.joinable || state.lobby_id != lobby_id) {
                            status = ERR_SKIP;
                        } else if (Time::get_singleton()->get_ticks_msec() >= service->publish_deadline) {
                            status = ERR_TIMEOUT;
                        }
                        if (status == OK) {
                            beacon.instantiate();
                            beacon->service_id = self;
                            beacon->id = event.m_ulBeaconID;
                            gate.beacon = beacon->id;
                            gate.host = self;
                            service->hosted = beacon;
                            service->hosted_lobby = lobby_id;
                            service->open_ceiling = seat_ceiling;
                            service->sdk_open_slots = slot_count;
                            service->next_host_update = 0;
                        } else if (event.m_ulBeaconID) {
                            destroy_beacon(runtime, event.m_ulBeaconID);
                        }
                    }
                    if (observer.is_valid()) {
                        observer->emit_signal("publication_completed", SteamPartyBeaconResult::create(status, beacon));
                    }
                })) {
        return SteamPartyBeaconResult::create(ERR_BUSY);
    }
    publishing.instantiate();
    publish_deadline = Time::get_singleton()->get_ticks_msec() + observation_ms;
    return Signal(publishing.ptr(), "publication_completed");
#else
    return SteamPartyBeaconResult::create(ERR_UNAVAILABLE);
#endif
}

Variant SteamParties::reserve(const Ref<SteamPartyListing> &listing) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamParties *parties = party_api(active);
    if (!parties) {
        return SteamListingResult::from_listing(ERR_UNAVAILABLE);
    }
    if (listing.is_null() || listing->app_id != active->get_app_id()) {
        return SteamListingResult::from_listing(ERR_INVALID_PARAMETER);
    }
    const auto fresh = read_beacon(active, listing->id);
    if (fresh.is_null() || fresh->owner != listing->owner || fresh->lobby != listing->lobby ||
            fresh->name_space != listing->name_space) {
        return SteamListingResult::from_listing(ERR_UNAUTHORIZED);
    }
    auto &gate = party_gate(active);
    if (gate.reserving != k_uAPICallInvalid || !active->can_track_call()) {
        return SteamListingResult::from_listing(ERR_BUSY);
    }
    const auto join = active->symbol<decltype(&SteamAPI_ISteamParties_JoinParty)>("SteamAPI_ISteamParties_JoinParty");
    if (!join) {
        return SteamListingResult::from_listing(ERR_UNAVAILABLE);
    }
    const SteamAPICall_t call = join(parties, fresh->id);
    if (call == k_uAPICallInvalid) {
        return SteamListingResult::from_listing(FAILED);
    }
    gate.reserving = call;
    const ObjectID self(get_instance_id());
    const std::weak_ptr<SteamRuntime> weak(active);
    if (!active->watch_call(call, JoinPartyCallback_t::k_iCallback, sizeof(JoinPartyCallback_t),
                [self, weak, call, fresh](const void *data) {
                    const auto runtime = weak.lock();
                    if (!runtime) {
                        return;
                    }
                    auto &gate = party_gate(runtime);
                    if (gate.reserving != call) {
                        return;
                    }
                    gate.reserving = k_uAPICallInvalid;
                    const auto service = party_service(self);
                    if (service.is_null() || service->reserving.is_null()) {
                        return;
                    }
                    const auto observer = std::exchange(service->reserving, {});
                    JoinPartyCallback_t event{};
                    if (data) {
                        std::memcpy(&event, data, sizeof(event));
                    }
                    Error status = data ? party_error(event.m_eResult) : FAILED;
                    Ref<SteamListing> route;
                    if (!service->active_runtime()) {
                        status = ERR_SKIP;
                    } else if (Time::get_singleton()->get_ticks_msec() >= service->reserve_deadline) {
                        status = ERR_TIMEOUT;
                    } else if (status == OK) {
                        if (event.m_ulBeaconID != fresh->id || event.m_SteamIDBeaconOwner.ConvertToUint64() != fresh->owner ||
                                bounded_text(event.m_rgchConnectString, sizeof(event.m_rgchConnectString)) !=
                                        connect_text(fresh->lobby)) {
                            status = ERR_INVALID_DATA;
                        } else {
                            route = SteamListing::party_lobby(fresh->lobby, fresh->owner, fresh->app_id, fresh->name_space);
                        }
                    }
                    observer->emit_signal("reservation_completed", SteamListingResult::from_listing(status, route));
                })) {
        return SteamListingResult::from_listing(ERR_BUSY);
    }
    reserving.instantiate();
    reserve_deadline = Time::get_singleton()->get_ticks_msec() + observation_ms;
    return Signal(reserving.ptr(), "reservation_completed");
#else
    return SteamListingResult::from_listing(ERR_UNAVAILABLE);
#endif
}

void SteamParties::retire_game_beacon() {
    if (hosted.is_valid()) {
        close_beacon(hosted.ptr(), ERR_SKIP);
    }
    if (const auto observer = std::exchange(publishing, {}); observer.is_valid()) {
        observer->emit_signal("publication_completed", SteamPartyBeaconResult::create(ERR_SKIP));
    }
}

Error SteamParties::close_beacon(const SteamPartyBeacon *beacon, Error reason) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    if (!active || hosted.ptr() != beacon || !beacon) {
        return ERR_UNAVAILABLE;
    }
    auto &gate = party_gate(active);
    if (gate.host != ObjectID(get_instance_id()) || gate.beacon != beacon->id) {
        return ERR_UNAVAILABLE;
    }
    const auto closing = std::exchange(hosted, {});
    const auto operation = std::exchange(slot_operation, {});
    hosted_lobby = String();
    reservations.clear();
    closing->service_id = ObjectID();
    const bool destroyed = destroy_beacon(active, closing->id);
    if (operation.is_valid()) {
        operation->finish(ERR_SKIP);
    }
    closing->emit_signal("closed", destroyed ? reason : FAILED);
    return destroyed ? OK : FAILED;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamPartySlotsOperation> SteamParties::set_open_slots(const SteamPartyBeacon *beacon, int64_t slots) {
    Ref<SteamPartySlotsOperation> operation;
    operation.instantiate();
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active && beacon && hosted.ptr() == beacon) {
        const auto state = host_state();
        const int32_t reserved = remaining_reservations(reservations, state.admitted);
        if (slots < 0 || slots > maximum_open_slots || slots + reserved > state.free_slots) {
            operation->finish(ERR_INVALID_PARAMETER);
        } else if (!state.joinable || state.lobby_id != hosted_lobby) {
            operation->finish(ERR_UNAVAILABLE);
        } else {
            const auto requested = submit_slots(active, static_cast<int32_t>(slots));
            if (!requested->is_finished() || requested->result()->is_ok()) {
                open_ceiling = static_cast<int32_t>(state.admitted.size() + reserved + slots);
            }
            return requested;
        }
        return operation;
    }
#endif
    operation->finish(ERR_UNAVAILABLE);
    return operation;
}

Ref<SteamPartySlotsOperation> SteamParties::submit_slots(const std::shared_ptr<SteamRuntime> &active, int32_t slots) {
    Ref<SteamPartySlotsOperation> operation;
    operation.instantiate();
#ifdef CRIMSON_WITH_STEAM
    if (slot_operation.is_valid() || !active->can_track_call()) {
        operation->finish(ERR_BUSY);
        return operation;
    }
    if (sdk_open_slots == slots) {
        operation->finish(OK);
        return operation;
    }
    ISteamParties *parties = party_api(active);
    const auto change = parties ? active->symbol<decltype(&SteamAPI_ISteamParties_ChangeNumOpenSlots)>(
                                          "SteamAPI_ISteamParties_ChangeNumOpenSlots") : nullptr;
    if (!change || hosted.is_null()) {
        operation->finish(ERR_UNAVAILABLE);
        return operation;
    }
    const auto beacon = hosted;
    const ObjectID self(get_instance_id());
    const SteamAPICall_t call = change(parties, beacon->id, static_cast<uint32_t>(slots));
    if (!active->watch_call(call, ChangeNumOpenSlotsCallback_t::k_iCallback, sizeof(ChangeNumOpenSlotsCallback_t),
                [self, beacon, operation, slots](const void *data) {
                    const auto service = party_service(self);
                    if (service.is_null() || service->hosted != beacon || service->slot_operation != operation) {
                        return;
                    }
                    service->slot_operation.unref();
                    ChangeNumOpenSlotsCallback_t event{};
                    if (data) {
                        std::memcpy(&event, data, sizeof(event));
                    }
                    const Error status = data ? party_error(event.m_eResult) : FAILED;
                    if (status == OK) {
                        service->sdk_open_slots = slots;
                    }
                    operation->finish(Time::get_singleton()->get_ticks_msec() >= service->slot_deadline ?
                            ERR_TIMEOUT : status);
                })) {
        operation->finish(FAILED);
        return operation;
    }
    slot_operation = operation;
    slot_deadline = Time::get_singleton()->get_ticks_msec() + observation_ms;
#else
    operation->finish(ERR_UNAVAILABLE);
#endif
    return operation;
}

Error SteamParties::finish_reservation(const SteamPartyBeacon *beacon,
        const Ref<SteamPartyReservation> &reservation, bool complete) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || hosted.ptr() != beacon || !beacon) {
        return ERR_UNAVAILABLE;
    }
    if (reservation.is_null() || reservation->beacon != beacon->id) {
        return ERR_INVALID_PARAMETER;
    }
    const auto found = reservations.find(reservation->account);
    if (found == reservations.end() || found->second != reservation) {
        return ERR_DOES_NOT_EXIST;
    }
    if (complete && !host_state().ready.has(reservation->get_steam_id())) {
        return ERR_UNAUTHORIZED;
    }
    ISteamParties *parties = party_api(active);
    const auto action = parties ? active->symbol<decltype(&SteamAPI_ISteamParties_OnReservationCompleted)>(
            complete ? "SteamAPI_ISteamParties_OnReservationCompleted" : "SteamAPI_ISteamParties_CancelReservation") :
            nullptr;
    if (!action) {
        return ERR_UNAVAILABLE;
    }
    reservations.erase(found);
    action(parties, beacon->id, reservation->account);
    if (!complete && sdk_open_slots < maximum_open_slots) {
        ++sdk_open_slots;
    }
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamParties::on_reservation(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || hosted.is_null() || !data || size != sizeof(ReservationNotificationCallback_t)) {
        return;
    }
    ReservationNotificationCallback_t event{};
    std::memcpy(&event, data, sizeof(event));
    const uint64_t account = event.m_steamIDJoiner.ConvertToUint64();
    if (hosted->id != event.m_ulBeaconID || !event.m_steamIDJoiner.IsValid() ||
            !event.m_steamIDJoiner.BIndividualAccount() || account == active->get_user_id() ||
            reservations.count(account)) {
        return;
    }
    const auto state = host_state();
    if (sdk_open_slots > 0) {
        --sdk_open_slots;
    }
    if (!state.joinable || state.lobby_id != hosted_lobby ||
            reservations.size() >= static_cast<size_t>(maximum_open_slots) ||
            (!state.admitted.has(String::num_uint64(account)) &&
                    state.free_slots <= remaining_reservations(reservations, state.admitted))) {
        ISteamParties *parties = party_api(active);
        const auto cancel = parties ? active->symbol<decltype(&SteamAPI_ISteamParties_CancelReservation)>(
                                            "SteamAPI_ISteamParties_CancelReservation") : nullptr;
        if (cancel) {
            cancel(parties, hosted->id, account);
            if (sdk_open_slots < maximum_open_slots) {
                ++sdk_open_slots;
            }
        }
        return;
    }
    Ref<SteamPartyReservation> reservation;
    reservation.instantiate();
    reservation->account = account;
    reservation->beacon = hosted->id;
    reservation->deadline = Time::get_singleton()->get_ticks_msec() + reservation_ms;
    reservations.emplace(account, reservation);
    const auto beacon = hosted;
    beacon->emit_signal("reservation_received", reservation);
#endif
}

void SteamParties::update_host(const std::shared_ptr<SteamRuntime> &active, uint64_t now) {
#ifdef CRIMSON_WITH_STEAM
    const auto state = host_state();
    const auto beacon = hosted;
    if (!state.joinable || state.lobby_id != hosted_lobby) {
        close_beacon(beacon.ptr(), ERR_SKIP);
        return;
    }
    std::vector<Ref<SteamPartyReservation>> settled;
    for (const auto &[account, reservation] : reservations) {
        if (state.ready.has(reservation->get_steam_id()) || now >= reservation->deadline) {
            settled.push_back(reservation);
        }
    }
    for (const auto &reservation : settled) {
        finish_reservation(beacon.ptr(), reservation, state.ready.has(reservation->get_steam_id()));
    }
    const int32_t allowed = (std::min)(state.free_slots, open_ceiling - static_cast<int32_t>(state.admitted.size()));
    const int32_t desired = (std::max)(0, allowed - remaining_reservations(reservations, state.admitted));
    if (slot_operation.is_null() && desired != sdk_open_slots) {
        submit_slots(active, desired);
    }
#endif
}

void SteamParties::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (publishing.is_valid() && now >= publish_deadline) {
        const auto observer = std::exchange(publishing, {});
        observer->emit_signal("publication_completed", SteamPartyBeaconResult::create(ERR_TIMEOUT));
    }
    if (reserving.is_valid() && now >= reserve_deadline) {
        const auto observer = std::exchange(reserving, {});
        observer->emit_signal("reservation_completed", SteamListingResult::from_listing(ERR_TIMEOUT));
    }
    if (slot_operation.is_valid() && now >= slot_deadline) {
        const auto operation = slot_operation;
        operation->finish(ERR_TIMEOUT);
    }
    if (!active_runtime()) {
        return;
    }
    auto &gate = party_gate(active);
    if (gate.beacon && gate.host == ObjectID() && now >= gate.next_cleanup) {
        destroy_beacon(active, gate.beacon);
    }
    if (hosted.is_valid() && now >= next_host_update) {
        next_host_update = now + host_tick_ms;
        update_host(active, now);
    }
#endif
}

} // namespace crimson::steam
