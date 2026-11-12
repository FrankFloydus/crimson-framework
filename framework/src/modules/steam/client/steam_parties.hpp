// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <functional>
#include <map>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
class SteamParties;
class SteamPartyBeacon;

// Decode the strict party locator form that preserves the SDK-observed beacon owner.
bool parse_steam_party_route(const godot::String &route, uint64_t &lobby, uint64_t &owner);

// Authority-supplied view of the exact published lobby and admitted arrivals.
struct SteamPartyHostState {
    // Native lobby ID for the active published game.
    godot::String lobby_id;
    // Project namespace used for matching discovered beacons.
    godot::StringName name_space;
    // Remaining authority-owned gameplay seats before beacon reservations.
    int32_t free_slots = 0;
    // Whether the exact route permits fresh reservations and admission.
    bool joinable = false;
    // Provider-verified accounts admitted to this match, including pending world setup.
    godot::PackedStringArray admitted;
    // Exact admitted accounts which have acknowledged the current world.
    godot::PackedStringArray ready;
};

// Detached location that Steam says can display a party beacon.
class SteamPartyLocation : public godot::RefCounted {
    GDCLASS(SteamPartyLocation, godot::RefCounted)

public:
    // Supported Steam location families.
    enum Kind {
        CHAT_GROUP = 1, // Steam Chat group.
    };
    // Return the opaque decimal location identifier.
    godot::String get_id() const;
    // Return the Steam location family.
    Kind get_kind() const;

protected:
    // Register the read-only location fields.
    static void _bind_methods();

private:
    friend class SteamParties;

    // Native opaque location ID.
    uint64_t id = 0;
};

// Detached Steam-owned beacon listing for the current AppID.
class SteamPartyListing : public godot::RefCounted {
    GDCLASS(SteamPartyListing, godot::RefCounted)

public:
    // Return the opaque decimal Steam beacon identifier.
    godot::String get_id() const;
    // Return the SDK-observed creator's SteamID64.
    godot::String get_owner_steam_id() const;
    // Return the detached discovery location.
    godot::Ref<SteamPartyLocation> get_location() const;

protected:
    // Register the read-only beacon discovery fields.
    static void _bind_methods();

private:
    friend class SteamParties;

    // Native beacon ID.
    uint64_t id = 0;
    // Exact individual Steam account from GetBeaconDetails.
    uint64_t owner = 0;
    // Compatible gameplay lobby encoded in this beacon's metadata.
    uint64_t lobby = 0;
    // Actual AppID of the client used for discovery.
    uint32_t app_id = 0;
    // Bounded game namespace from the compatible route.
    godot::StringName name_space;
    // Location snapshot detached from Steam.
    godot::Ref<SteamPartyLocation> location;
};

// Exact reservation issued to one live host beacon by Steam.
class SteamPartyReservation : public godot::RefCounted {
    GDCLASS(SteamPartyReservation, godot::RefCounted)

public:
    // Return the reserved individual's SteamID64.
    godot::String get_steam_id() const;
    // Return the opaque beacon ID under which Steam reserved the slot.
    godot::String get_beacon_id() const;

protected:
    // Register read-only reservation identifiers.
    static void _bind_methods();

private:
    friend class SteamParties;

    // SDK-verified reservation identity.
    uint64_t account = 0;
    // Exact beacon this reservation belongs to.
    uint64_t beacon = 0;
    // Monotonic expiration for an abandoned reservation.
    uint64_t deadline = 0;
};

// Result of enumerating eligible Steam party locations.
class SteamPartyLocationsResult : public SteamResult {
    GDCLASS(SteamPartyLocationsResult, SteamResult)

public:
    // Return the detached read-only location snapshot.
    godot::TypedArray<SteamPartyLocation> get_value() const;

protected:
    // Register the typed snapshot.
    static void _bind_methods();

private:
    friend class SteamParties;

    // Create a terminal typed locations outcome.
    static godot::Ref<SteamPartyLocationsResult> create(godot::Error error,
            const godot::TypedArray<SteamPartyLocation> &locations = {});

    // Bounded copied location rows.
    godot::TypedArray<SteamPartyLocation> value;
};

// Result of enumerating Steam beacons with compatible game routes.
class SteamPartyListingsResult : public SteamResult {
    GDCLASS(SteamPartyListingsResult, SteamResult)

public:
    // Return the detached read-only listing snapshot.
    godot::TypedArray<SteamPartyListing> get_value() const;

protected:
    // Register the typed snapshot.
    static void _bind_methods();

private:
    friend class SteamParties;

    // Create a terminal typed listing outcome.
    static godot::Ref<SteamPartyListingsResult> create(godot::Error error,
            const godot::TypedArray<SteamPartyListing> &listings = {});

    // Bounded copied listing rows.
    godot::TypedArray<SteamPartyListing> value;
};

// Result of creating one host-controlled Steam party beacon.
class SteamPartyBeaconResult : public SteamResult {
    GDCLASS(SteamPartyBeaconResult, SteamResult)

public:
    // Return the new scoped beacon on success.
    godot::Ref<SteamPartyBeacon> get_value() const;

protected:
    // Register the typed result payload.
    static void _bind_methods();

private:
    friend class SteamParties;

    // Create a terminal typed publication outcome.
    static godot::Ref<SteamPartyBeaconResult> create(godot::Error error,
            const godot::Ref<SteamPartyBeacon> &beacon = {});

    // Exact owned beacon, cleared by scope retirement.
    godot::Ref<SteamPartyBeacon> value;
};

// Retained observer of one explicit beacon slot update.
class SteamPartySlotsOperation : public godot::RefCounted {
    GDCLASS(SteamPartySlotsOperation, godot::RefCounted)

public:
    // Return whether the observer has settled.
    bool is_finished() const;
    // Return the terminal result, or null while pending.
    godot::Ref<SteamBoolResult> result() const;
    // Await completion or read an already settled result.
    godot::Variant wait();
    // Cancel observation. The SDK may still apply the submitted change.
    godot::Error cancel();

protected:
    // Register the retained typed operation.
    static void _bind_methods();

private:
    friend class SteamParties;
    friend class SteamPartyBeacon;

    // Publish the terminal result exactly once.
    void finish(godot::Error error);

    // Retained result available after completion.
    godot::Ref<SteamBoolResult> outcome;
};

// Exact locally published party beacon with bounded reservation controls.
class SteamPartyBeacon : public godot::RefCounted {
    GDCLASS(SteamPartyBeacon, godot::RefCounted)

public:
    // Return the opaque beacon identifier for this publication.
    godot::String get_id() const;
    // Select a new advertised ceiling, constrained by authority-owned free seats.
    godot::Ref<SteamPartySlotsOperation> change_open_slots(int64_t slots);
    // Acknowledge a reservation only after this account reached the current world.
    godot::Error complete(const godot::Ref<SteamPartyReservation> &reservation);
    // Return an exact in-flight seat to Steam.
    godot::Error cancel(const godot::Ref<SteamPartyReservation> &reservation);
    // Destroy the exact owned beacon and retire pending reservations.
    godot::Error close();

protected:
    // Register explicit host controls and lifecycle observations.
    static void _bind_methods();

private:
    friend class SteamParties;

    // Resolve the exact native facade while the publication is live.
    godot::Ref<SteamParties> service() const;

    // Stable identity of the owning client scope.
    godot::ObjectID service_id;
    // Native beacon ID for this publication.
    uint64_t id = 0;
};

// Carries typed completion for immediate-await Party methods.
class SteamPartyObservation : public godot::RefCounted {
    GDCLASS(SteamPartyObservation, godot::RefCounted)

protected:
    // Register distinct typed completion signals for publication and reservation.
    static void _bind_methods();
};

// Standalone Steam Parties discovery and Game-owned beacon publication.
class SteamParties : public godot::RefCounted {
    GDCLASS(SteamParties, godot::RefCounted)

public:
    // Read currently eligible Steam chat locations.
    godot::Ref<SteamPartyLocationsResult> locations() const;
    // Read visible beacons whose metadata names a compatible gameplay route.
    godot::Ref<SteamPartyListingsResult> beacons() const;
    // Await a single native beacon bound to the current eligible hosted game.
    godot::Variant publish(const godot::Ref<SteamListing> &listing,
            const godot::Ref<SteamPartyLocation> &location, int64_t open_slots);
    // Await a scoped reservation and an owner-bound lobby locator.
    godot::Variant reserve(const godot::Ref<SteamPartyListing> &listing);

    // Inject the exact game attachment's authority-owned published lobby and readiness.
    void set_game_source(std::function<SteamPartyHostState()> source);
    // Retire any publication while the owning game removes its listing.
    void retire_game_beacon();

protected:
    // Register typed Party methods and discovery invalidation.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamPartyBeacon;

    // Capture the runtime and exact scope without accessing Party data eagerly.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Retire pending observers, subscriptions, and the owned beacon.
    void detach();
    // Process short observation deadlines, bounded reservations, and authority seat updates.
    void tick();
    // Resolve the live Steam runtime only from the main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Read a fresh exact SDK beacon before displaying or acting on it.
    godot::Ref<SteamPartyListing> read_beacon(const std::shared_ptr<SteamRuntime> &active, uint64_t id) const;
    // Validate current host eligibility and remaining seats for this publication.
    SteamPartyHostState host_state() const;
    // Apply an exact slot update constrained by authoritative availability.
    godot::Ref<SteamPartySlotsOperation> set_open_slots(const SteamPartyBeacon *beacon, int64_t slots);
    // Submit an SDK slot change and track its exact completion.
    godot::Ref<SteamPartySlotsOperation> submit_slots(const std::shared_ptr<SteamRuntime> &active, int32_t slots);
    // Return, cancel or verify the exact live reservation.
    godot::Error finish_reservation(const SteamPartyBeacon *beacon,
            const godot::Ref<SteamPartyReservation> &reservation, bool complete);
    // Destroy the native beacon and settle observers before emitting closure.
    godot::Error close_beacon(const SteamPartyBeacon *beacon, godot::Error reason);
    // Capture one exact SDK reservation from its copied native callback.
    void on_reservation(const void *data, size_t size);
    // Reconcile the authority roster, ready arrivals and advertised seats.
    void update_host(const std::shared_ptr<SteamRuntime> &active, uint64_t now);

    // Weak runtime lease borrowed from the owning client.
    std::weak_ptr<SteamRuntime> runtime;
    // Lifetime of the exact SteamClient start.
    std::weak_ptr<SteamClientScope> client_scope;
    // Game authority snapshot, absent on standalone scopes and guests.
    std::function<SteamPartyHostState()> game_source;
    // Subscription to exact Steam reservation notifications.
    uint64_t reservation_subscription = 0;
    // Subscription to Steam beacon list invalidations.
    uint64_t beacons_subscription = 0;
    // Subscription to Steam eligible location invalidations.
    uint64_t locations_subscription = 0;
    // Observer for the current publication, if any.
    godot::Ref<SteamPartyObservation> publishing;
    // Absolute local observation deadline for the current publication.
    uint64_t publish_deadline = 0;
    // Observer for the current reserve attempt, if any.
    godot::Ref<SteamPartyObservation> reserving;
    // Absolute local observation deadline for the current reservation.
    uint64_t reserve_deadline = 0;
    // Exact active beacon in this scope.
    godot::Ref<SteamPartyBeacon> hosted;
    // Hosted lobby ID used to reject retirement and replacement races.
    godot::String hosted_lobby;
    // User-selected ceiling before admitted players and reservations consume space.
    int32_t open_ceiling = 0;
    // Number of open slots last accepted by the SDK, including its own reservation updates.
    int32_t sdk_open_slots = 0;
    // In-flight reservations by the verified individual SteamID.
    std::map<uint64_t, godot::Ref<SteamPartyReservation>> reservations;
    // Exact asynchronous slot operation retained until SDK completion.
    godot::Ref<SteamPartySlotsOperation> slot_operation;
    // Absolute observation deadline for the slot change.
    uint64_t slot_deadline = 0;
    // Bounded cadence for authority seat and readiness reconciliation.
    uint64_t next_host_update = 0;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamPartyLocation::Kind);
