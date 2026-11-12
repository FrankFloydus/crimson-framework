<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Save browser presentation

`SavePresentation` is a native Godot class with static methods. It creates and reads display-only save metadata. The host chooses save slot IDs, labels, dates, context fields, and whether to attach a screenshot. The save coordinator stores the metadata with the generation and returns it during inspection.

| Method | Contract |
| --- | --- |
| `make_metadata(label, saved_at_utc, context, screenshot = null) -> Dictionary` | Sanitize and bound display text, store the timestamp as Unix seconds, and encode a preview when it fits the metadata budget. Pass an `Image` captured by the host. An unavailable or unsupported image yields metadata without a preview. |
| `describe(entry, date_pattern = "%Y-%m-%d %H:%M UTC", utc_offset_minutes = 0) -> Dictionary` | Accept an inspection entry or its `meta` dictionary and return `label`, `time_text`, `context`, and a `preview` texture. A missing or malformed preview yields `null`. |
| `format_date(unix_seconds, pattern, utc_offset_minutes = 0) -> String` | Format an existing save timestamp with a host-provided fixed UTC offset. An out-of-range date or offset yields an empty string. |

Call the presentation methods on the main thread. The optional preview uses the original compatible encodings. The method tries 96 × 54 RGB565 deflate, then 64 × 36 or 48 × 27 raw RGB565. Pass the returned metadata to `SaveCoordinator::request_save` or `request_autosave`. Keep the `sequence` and `id` from slot inspection alongside the display fields to load the selected generation.

The date patterns support `%Y`, `%m`, `%d`, `%H`, `%M`, `%S`, `%z` for the numeric offset, and `%%` for a literal percent sign. Supply the offset applicable to the date. The host owns translated month names and daylight-saving rules.
