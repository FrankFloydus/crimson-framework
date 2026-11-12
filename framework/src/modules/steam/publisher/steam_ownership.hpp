// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"
#include "modules/steam/publisher/steam_publisher.hpp"

#include <godot_cpp/variant/packed_int64_array.hpp>

namespace crimson::steam {

// Ownership information detached from one verified, freshly issued encrypted app ticket.
class SteamOwnership : public godot::RefCounted {
    GDCLASS(SteamOwnership, godot::RefCounted)

public:
    // Return the verified player identity issued by this trusted publisher.
    godot::Ref<SteamIdentity> get_identity() const;
    // Report whether this receipt proves base app ownership.
    bool owns_app() const;
    // Return the ticket's checked issue time in Unix seconds.
    int64_t get_issued_at_unix() const;
    // Return the DLC AppIDs requested for this check.
    godot::PackedInt64Array get_checked_dlc_ids() const;
    // Return the checked DLC AppIDs that the ticket confirms.
    godot::PackedInt64Array get_owned_dlc_ids() const;
    // Report whether this receipt confirms ownership of one checked DLC.
    bool owns_dlc(int64_t app_id) const;

protected:
    // Bind the detached ownership fields and DLC lookup.
    static void _bind_methods();

private:
    friend class SteamPublisherAuth;

    // Exact publisher identity minted after the durable challenge is consumed.
    godot::Ref<SteamIdentity> identity;
    // Base app ownership reported by the verified ticket.
    bool app_owned = false;
    // Checked ticket issue time in Unix seconds.
    int64_t issued_at_unix = 0;
    // DLC AppIDs checked against the same decrypted ticket.
    godot::PackedInt64Array checked_dlc_ids;
    // Requested DLC AppIDs that Steam confirms in this snapshot.
    godot::PackedInt64Array owned_dlc_ids;
};

// Outcome of encrypted app ticket verification and one-time challenge consumption.
class SteamOwnershipResult : public SteamResult {
    GDCLASS(SteamOwnershipResult, SteamResult)

public:
    // Return the ownership receipt on success, or null on failure.
    godot::Ref<SteamOwnership> get_value() const;

protected:
    // Bind the read-only ownership receipt.
    static void _bind_methods();

private:
    friend class SteamPublisherAuth;

    // Create a terminal result without retaining ticket, challenge, or secret bytes.
    static godot::Ref<SteamOwnershipResult> create(godot::Error error, const godot::Ref<SteamOwnership> &receipt = {});

    // Checked ownership receipt, set only after all authority checks succeed.
    godot::Ref<SteamOwnership> value;
};

} // namespace crimson::steam
