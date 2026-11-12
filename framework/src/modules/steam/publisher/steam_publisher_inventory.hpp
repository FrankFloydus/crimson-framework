// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_inventory_types.hpp"
#include "modules/steam/publisher/steam_publisher.hpp"

#include <godot_cpp/variant/packed_int32_array.hpp>

namespace crimson::steam {

// Steam's acknowledgment of a request to open a trade between two verified users.
class SteamTradeSession : public godot::RefCounted {
    GDCLASS(SteamTradeSession, godot::RefCounted)

public:
    // Return the first verified participant as a decimal SteamID64.
    godot::String get_player_id() const;
    // Return the second verified participant as a decimal SteamID64.
    godot::String get_partner_id() const;
    // Return whether Steam acknowledged the request to start the trade.
    bool is_acknowledged() const;

protected:
    // Bind the immutable trade acknowledgment fields.
    static void _bind_methods();

private:
    friend class SteamPublisherInventory;

    // Verified first participant.
    godot::String player_id;
    // Verified second participant.
    godot::String partner_id;
    // True for a recognized affirmative Steam reply.
    bool acknowledged = false;
};

// Typed result for one trusted Steam trade request.
class SteamTradeSessionResult : public SteamResult {
    GDCLASS(SteamTradeSessionResult, SteamResult)

public:
    // Return the accepted trade request, or null when its outcome was not confirmed.
    godot::Ref<SteamTradeSession> get_value() const;

protected:
    // Bind the typed trade result.
    static void _bind_methods();

private:
    friend class SteamPublisherInventory;

    // Create a detached result with an optional Steam acknowledgment.
    static godot::Ref<SteamTradeSessionResult> create(godot::Error error,
            const godot::Ref<SteamTradeSession> &session = {});

    // The recognized acknowledgment, when available.
    godot::Ref<SteamTradeSession> value;
};

// Reads and changes app inventory through the trusted publisher transport.
class SteamPublisherInventory : public godot::RefCounted {
    GDCLASS(SteamPublisherInventory, godot::RefCounted)

public:
    // Await an app inventory snapshot for a player verified by this publisher.
    godot::Variant items(const godot::Ref<SteamIdentity> &player);
    // Await grant results using the game's durable request ID for the exact award.
    godot::Variant grant(const godot::Ref<SteamIdentity> &player,
            const godot::PackedInt32Array &definition_ids, const godot::String &request_id);
    // Await consumption using the game's durable request ID for this exact action.
    godot::Variant consume(const godot::Ref<SteamIdentity> &player, const godot::String &instance_id,
            int64_t quantity, const godot::String &request_id);
    // Await Steam's acknowledgment of a trade request between two verified participants.
    godot::Variant start_trade(const godot::Ref<SteamIdentity> &player, const godot::Ref<SteamIdentity> &partner);

protected:
    // Register the four directly awaited publisher inventory actions.
    static void _bind_methods();

private:
    friend class SteamPublisher;

    // Resolve a fixed grant or consumption route and settle the typed change.
    godot::Variant mutate(const std::shared_ptr<SteamPublisherState> &issuer, const SteamPublisherRequest &request,
            uint64_t player, uint64_t consumed_instance = 0);
    // Copy a validated present row into the shared immutable inventory DTO.
    static godot::Ref<SteamItemInstance> make_item(uint64_t id, uint64_t definition, uint64_t quantity,
            uint64_t flags);

    // Exact issuing publisher without extending its lifetime.
    std::weak_ptr<SteamPublisherState> scope;
};

} // namespace crimson::steam
