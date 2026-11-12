// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstddef>

namespace crimson::steam {

class SteamRuntime;

// Copies modal input while its native callback owns the Steam text buffer.
struct SteamModalDismissal {
    // A malformed record or unrelated AppID cannot release the process keyboard lease.
    bool matches = false;
    // User accepted the modal dialog, including an intentionally empty submission.
    bool submitted = false;
    // Text copy status before invoking game-side observers.
    godot::Error error = godot::OK;
    // Owned text after Steam's callback buffer becomes invalid.
    godot::String text;
};

// Capture the exact modal lease's outcome before returning the callback buffer to Steam.
SteamModalDismissal copy_steam_modal_dismissal(SteamRuntime &active, const void *record, size_t size);

} // namespace crimson::steam

#endif
