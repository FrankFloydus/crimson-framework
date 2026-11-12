// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace crimson::save {

// Build and describe bounded save-browser metadata for Godot.
class SavePresentation : public godot::RefCounted {
    GDCLASS(SavePresentation, godot::RefCounted)

public:
    // Build display metadata from a label, timestamp, context, and optional screenshot.
    static godot::Dictionary make_metadata(const godot::String &label, int64_t saved_at_utc,
            const godot::Dictionary &context, const godot::Ref<godot::Image> &screenshot = {});
    // Convert an inspected entry into display fields and an optional preview texture.
    static godot::Dictionary describe(const godot::Dictionary &entry,
            const godot::String &date_pattern = "%Y-%m-%d %H:%M UTC", int64_t utc_offset_minutes = 0);
    // Format a supported Unix timestamp with a display pattern and UTC offset.
    static godot::String format_date(int64_t unix_seconds, const godot::String &pattern,
            int64_t utc_offset_minutes = 0);

protected:
    // Expose save-browser helpers as static Godot methods.
    static void _bind_methods();
};

} // namespace crimson::save
