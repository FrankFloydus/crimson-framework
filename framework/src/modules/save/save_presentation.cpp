// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "save_presentation.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::save {
namespace {

using namespace godot;

constexpr char default_date_pattern[] = "%Y-%m-%d %H:%M UTC";
constexpr int64_t min_unix_seconds = -62135596800;
constexpr int64_t max_unix_seconds = 253402300799;
constexpr int64_t max_offset_minutes = 1439;

// Leave room for the coordinator's kind and capture_revision fields.
constexpr int64_t max_metadata_bytes = 7168;
constexpr int64_t max_label_bytes = 256;
constexpr int64_t max_context_key_bytes = 32;
constexpr int64_t max_context_value_bytes = 128;
constexpr int64_t max_context_fields = 8;
constexpr int64_t max_context_scan = 32;
constexpr int64_t max_preview_bytes = 4608;
constexpr int64_t max_base64_length = 6144;
constexpr char deflate_codec[] = "rgb565-deflate";
constexpr char raw_codec[] = "rgb565-raw";

const Vector2i deflate_size(96, 54);
const Vector2i raw_size(64, 36);
const Vector2i fallback_size(48, 27);

// Check whether a timestamp falls within the supported calendar range.
bool valid_unix_seconds(int64_t value) {
    return value >= min_unix_seconds && value <= max_unix_seconds;
}

// Exclude control, directional formatting, and surrogate characters from display text.
bool displayable_code_point(char32_t point) {
    return point >= 0x20 && !(point >= 0x7f && point <= 0x9f) && point != 0x061c &&
            point != 0x200e && point != 0x200f && !(point >= 0x2028 && point <= 0x202e) &&
            !(point >= 0x2066 && point <= 0x2069) && !(point >= 0xd800 && point <= 0xdfff);
}

// Return the UTF-8 byte width of one displayable Unicode code point.
int utf8_width(char32_t point) {
    if (point >= 0x10000) {
        return 4;
    }
    if (point >= 0x800) {
        return 3;
    }
    return point >= 0x80 ? 2 : 1;
}

// Clean and truncate a text value within its UTF-8 display budget.
String safe_text(const Variant &value, int64_t remaining_bytes) {
    if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
        return {};
    }

    const String text = value;
    const int64_t scan_limit = std::min(text.length(), remaining_bytes * 4);
    String result;
    for (int64_t index = 0; index < scan_limit; ++index) {
        const char32_t point = text[index];
        if (!displayable_code_point(point)) {
            continue;
        }
        const int width = utf8_width(point);
        if (width > remaining_bytes) {
            break;
        }
        result += String::chr(point);
        remaining_bytes -= width;
    }
    return result.strip_edges();
}

// Build a bounded set of sanitized display fields from save metadata.
Dictionary safe_context(const Dictionary &source) {
    Dictionary result;
    const Variant values = source;
    Variant iterator;
    bool valid = false;
    bool has_key = values.iter_init(iterator, valid);
    for (int64_t index = 0; has_key && valid && index < max_context_scan && result.size() < max_context_fields; ++index) {
        const Variant key = values.iter_get(iterator, valid);
        if (!valid) {
            break;
        }
        has_key = values.iter_next(iterator, valid);
        const String name = safe_text(key, max_context_key_bytes);
        if (name.is_empty() || result.has(name)) {
            continue;
        }

        const Variant &value = source[key];
        switch (value.get_type()) {
            case Variant::BOOL:
            case Variant::INT:
                result[name] = UtilityFunctions::str(value);
                break;
            case Variant::FLOAT:
                if (std::isfinite(static_cast<double>(value))) {
                    result[name] = safe_text(UtilityFunctions::str(value), max_context_value_bytes);
                }
                break;
            case Variant::STRING:
            case Variant::STRING_NAME:
                result[name] = safe_text(value, max_context_value_bytes);
                break;
            default:
                break;
        }
    }
    return result;
}

// Convert a normalized color channel to a bounded integer component.
int quantize(double channel, int maximum) {
    return std::isfinite(channel) ? static_cast<int>(std::round(std::clamp(channel, 0.0, 1.0) * maximum)) : 0;
}

// Sample an image into a fixed RGB565 thumbnail while retaining its aspect ratio.
PackedByteArray sample_rgb565(const Ref<Image> &image, Vector2i size) {
    PackedByteArray raw;
    raw.resize(size.x * size.y * 2);
    raw.fill(0);
    uint8_t *pixels = raw.ptrw();

    const Vector2i source = image->get_size();
    const double scale = std::min(static_cast<double>(size.x) / source.x, static_cast<double>(size.y) / source.y);
    const int width = std::clamp(static_cast<int>(std::round(source.x * scale)), 1, size.x);
    const int height = std::clamp(static_cast<int>(std::round(source.y * scale)), 1, size.y);
    const int left = (size.x - width) / 2;
    const int top = (size.y - height) / 2;
    for (int y = 0; y < height; ++y) {
        const int source_y = static_cast<int>((y + 0.5) * source.y / height);
        for (int x = 0; x < width; ++x) {
            const int source_x = static_cast<int>((x + 0.5) * source.x / width);
            const Color color = image->get_pixel(source_x, source_y);
            const int pixel = (quantize(color.r, 31) << 11) | (quantize(color.g, 63) << 5) | quantize(color.b, 31);
            const int offset = ((top + y) * size.x + left + x) * 2;
            pixels[offset] = static_cast<uint8_t>(pixel & 255);
            pixels[offset + 1] = static_cast<uint8_t>(pixel >> 8);
        }
    }
    return raw;
}

// Check whether a character belongs to the Base64 data alphabet.
bool base64_digit(char32_t point) {
    return (point >= 'A' && point <= 'Z') || (point >= 'a' && point <= 'z') ||
            (point >= '0' && point <= '9') || point == '+' || point == '/';
}

// Validate bounded Base64 length, alphabet, and trailing padding.
bool valid_base64(const String &text) {
    const int64_t length = text.length();
    if (length < 4 || length > max_base64_length || length % 4 != 0) {
        return false;
    }
    int64_t index = 0;
    while (index < length && base64_digit(text[index])) {
        ++index;
    }
    if (length - index > 2) {
        return false;
    }
    while (index < length && text[index] == '=') {
        ++index;
    }
    return index == length;
}

// Check the zlib method and header flags expected by preview decompression.
bool valid_zlib_header(const PackedByteArray &bytes) {
    return bytes.size() >= 6 && (bytes[0] & 15) == 8 && (bytes[0] >> 4) <= 7 &&
            (bytes[1] & 32) == 0 && ((bytes[0] << 8) | bytes[1]) % 31 == 0;
}

// Validate preview metadata and decode a bounded RGB565 texture.
Ref<ImageTexture> decode_preview(const Variant &value) {
    if (value.get_type() != Variant::DICTIONARY) {
        return {};
    }

    const Dictionary preview = value;
    const Variant codec_value = preview.get("codec", Variant());
    const Variant width_value = preview.get("width", Variant());
    const Variant height_value = preview.get("height", Variant());
    const Variant data_value = preview.get("data", Variant());
    if (codec_value.get_type() != Variant::STRING || width_value.get_type() != Variant::INT ||
            height_value.get_type() != Variant::INT || data_value.get_type() != Variant::STRING) {
        return {};
    }

    const String codec = codec_value;
    const int64_t width = width_value;
    const int64_t height = height_value;
    const bool compressed = codec == deflate_codec;
    const bool raw_dimensions = (width == raw_size.x && height == raw_size.y) ||
            (width == fallback_size.x && height == fallback_size.y);
    if (compressed ? width != deflate_size.x || height != deflate_size.y : codec != raw_codec || !raw_dimensions) {
        return {};
    }

    const String data = data_value;
    if (!valid_base64(data)) {
        return {};
    }
    Marshalls *marshalls = Marshalls::get_singleton();
    PackedByteArray raw = marshalls->base64_to_raw(data);
    if (raw.is_empty() || marshalls->raw_to_base64(raw) != data) {
        return {};
    }

    const int64_t pixel_count = width * height;
    if (compressed) {
        if (!valid_zlib_header(raw)) {
            return {};
        }
        raw = raw.decompress(pixel_count * 2, FileAccess::COMPRESSION_DEFLATE);
    }
    if (raw.size() != pixel_count * 2) {
        return {};
    }

    PackedByteArray rgb;
    rgb.resize(pixel_count * 3);
    uint8_t *pixels = rgb.ptrw();
    for (int64_t index = 0; index < pixel_count; ++index) {
        const int packed = raw[index * 2] | (raw[index * 2 + 1] << 8);
        const int red = packed >> 11;
        const int green = (packed >> 5) & 63;
        const int blue = packed & 31;
        pixels[index * 3] = static_cast<uint8_t>((red << 3) | (red >> 2));
        pixels[index * 3 + 1] = static_cast<uint8_t>((green << 2) | (green >> 4));
        pixels[index * 3 + 2] = static_cast<uint8_t>((blue << 3) | (blue >> 2));
    }
    return ImageTexture::create_from_image(Image::create_from_data(width, height, false, Image::FORMAT_RGB8, rgb));
}

// Format a calendar field with the required zero padding.
String padded_date_field(const Dictionary &date, const char *key, int digits = 2) {
    return String::num_int64(static_cast<int64_t>(date[key])).pad_zeros(digits);
}

} // namespace

using namespace godot;

Dictionary SavePresentation::make_metadata(const String &label, int64_t saved_at_utc,
        const Dictionary &context, const Ref<Image> &screenshot) {
    Dictionary metadata;
    metadata["label"] = safe_text(label, max_label_bytes);
    metadata["context"] = safe_context(context);
    if (valid_unix_seconds(saved_at_utc)) {
        metadata["saved_at_utc"] = saved_at_utc;
    }
    if (screenshot.is_null() || screenshot->is_empty() || screenshot->is_compressed()) {
        return metadata;
    }

    for (const Vector2i size : {deflate_size, raw_size, fallback_size}) {
        PackedByteArray bytes = sample_rgb565(screenshot, size);
        const bool compressed = size == deflate_size;
        if (compressed) {
            bytes = bytes.compress(FileAccess::COMPRESSION_DEFLATE);
        }
        if (bytes.is_empty() || bytes.size() > max_preview_bytes) {
            continue;
        }

        Dictionary preview;
        preview["codec"] = compressed ? deflate_codec : raw_codec;
        preview["width"] = size.x;
        preview["height"] = size.y;
        preview["data"] = Marshalls::get_singleton()->raw_to_base64(bytes);
        metadata["preview"] = preview;
        if (JSON::stringify(metadata).utf8().length() <= max_metadata_bytes) {
            return metadata;
        }
        metadata.erase("preview");
    }
    return metadata;
}

Dictionary SavePresentation::describe(const Dictionary &entry, const String &date_pattern, int64_t utc_offset_minutes) {
    const Variant source = entry.get("meta", entry);
    const Dictionary metadata = source.get_type() == Variant::DICTIONARY ? static_cast<Dictionary>(source) : Dictionary();
    const Variant timestamp = metadata.get("saved_at_utc", Variant());
    const Variant raw_context = metadata.get("context", Variant());
    Dictionary description;
    description["label"] = safe_text(metadata.get("label", Variant()), max_label_bytes);
    description["time_text"] = timestamp.get_type() == Variant::INT ?
            format_date(static_cast<int64_t>(timestamp), date_pattern, utc_offset_minutes) : String();
    description["context"] = raw_context.get_type() == Variant::DICTIONARY ?
            safe_context(static_cast<Dictionary>(raw_context)) : Dictionary();
    description["preview"] = decode_preview(metadata.get("preview", Variant()));
    return description;
}

String SavePresentation::format_date(int64_t unix_seconds, const String &pattern, int64_t utc_offset_minutes) {
    if (!valid_unix_seconds(unix_seconds) || utc_offset_minutes < -max_offset_minutes ||
            utc_offset_minutes > max_offset_minutes) {
        return {};
    }
    const int64_t adjusted = unix_seconds + utc_offset_minutes * 60;
    if (!valid_unix_seconds(adjusted)) {
        return {};
    }

    Time *time = Time::get_singleton();
    const Dictionary date = time->get_datetime_dict_from_unix_time(adjusted);
    const String effective_pattern = pattern == default_date_pattern && utc_offset_minutes != 0 ?
            String("%Y-%m-%d %H:%M %z") : pattern;
    String output;
    for (int64_t index = 0; index < effective_pattern.length(); ++index) {
        const char32_t point = effective_pattern[index];
        if (point != '%' || index + 1 == effective_pattern.length()) {
            output += String::chr(point);
            continue;
        }
        const char32_t token = effective_pattern[++index];
        switch (token) {
            case 'Y': output += padded_date_field(date, "year", 4); break;
            case 'm': output += padded_date_field(date, "month"); break;
            case 'd': output += padded_date_field(date, "day"); break;
            case 'H': output += padded_date_field(date, "hour"); break;
            case 'M': output += padded_date_field(date, "minute"); break;
            case 'S': output += padded_date_field(date, "second"); break;
            case 'z': output += time->get_offset_string_from_offset_minutes(utc_offset_minutes).replace(":", ""); break;
            case '%': output += "%"; break;
            default: output += String("%") + String::chr(token); break;
        }
    }
    return output;
}

void SavePresentation::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("make_metadata", "label", "saved_at_utc", "context", "screenshot"),
            &SavePresentation::make_metadata, DEFVAL(Variant()));
    ClassDB::bind_static_method(get_class_static(), D_METHOD("describe", "entry", "date_pattern", "utc_offset_minutes"),
            &SavePresentation::describe, DEFVAL(String(default_date_pattern)), DEFVAL(0));
    ClassDB::bind_static_method(get_class_static(), D_METHOD("format_date", "unix_seconds", "pattern", "utc_offset_minutes"),
            &SavePresentation::format_date, DEFVAL(0));
}

} // namespace crimson::save
