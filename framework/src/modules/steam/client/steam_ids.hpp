// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include <godot_cpp/variant/string.hpp>
#include <steam_api_common.h>

#include <charconv>
#include <cstdint>

namespace crimson::steam {

// Read a nonzero unsigned Steam identifier without truncating it to a signed Godot integer.
inline bool parse_steam_id(const godot::String &text, uint64_t &id) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const godot::CharString encoded = text.utf8();
    const char *const start = encoded.get_data();
    const char *const end = start + encoded.length();
    const auto parsed = std::from_chars(start, end, id);
    return parsed.ec == std::errc() && parsed.ptr == end && id != 0;
}

// Require an individual Steam account for player-directed actions.
inline bool parse_steam_account(const godot::String &text, uint64_t &id) {
    if (!parse_steam_id(text, id)) {
        return false;
    }
    const CSteamID account(id);
    return account.IsValid() && account.GetEAccountType() == k_EAccountTypeIndividual;
}

} // namespace crimson::steam

#endif
