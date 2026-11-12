// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstddef>
#include <cstdint>

namespace crimson::steam {

class SteamRuntime;

// A normalized Steam HTML event with no borrowed native memory.
struct SteamHtmlCallback {
    // Exact native callback kind for answering Steam's request.
    int kind = 0;
    // Handle of a browser owned by this client runtime.
    uint32_t browser = 0;
    // Requested or loaded URL, empty when unavailable or malformed.
    godot::String url;
    // Bounded copied title or link target depending on the callback kind.
    godot::String detail;
    // True if navigation target text was safely decoded.
    bool valid_target = false;
    // A full frame in RGBA8 after the SDK returns its buffer.
    godot::PackedByteArray rgba;
    // Frame dimensions in browser-local pixels.
    godot::Vector2i size;
    // Steam's monotonically advancing load serial within this browser.
    uint32_t page_serial = 0;
};

// Report whether a callback requires typed Steam HTML decoding instead of a shallow byte copy.
bool is_steam_html_callback(int kind);
// Accept browser dimensions within the shared per-frame pixel budget.
bool valid_steam_html_size(const godot::Vector2i &size);
// Copy bounded records for owned browsers before Steam releases the manual callback buffer.
SteamHtmlCallback copy_steam_html_callback(SteamRuntime &active, int kind, const void *record, size_t size);

} // namespace crimson::steam

#endif
