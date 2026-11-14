// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "wire_codec.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <map>
#include <optional>
#include <utility>

namespace crimson::replication {

// Default byte budget for a small gameplay message, before transport overhead.
inline constexpr int64_t default_codec_bytes = 1200;
// Maximum byte budget supported by the script-facing codec utilities.
inline constexpr int64_t max_codec_bytes = 1024 * 1024;

// Owns the bounded byte buffer into which a script writes typed fields.
class NetworkWriter : public godot::RefCounted {
    GDCLASS(NetworkWriter, godot::RefCounted)

    // Owns the storage referenced by the native writer until the result is sealed.
    godot::PackedByteArray bytes;
    // Native encoder over the fixed-capacity storage.
    std::optional<WireWriter> writer;
    // First writer error, or unconfigured until created with a budget.
    godot::Error error = godot::ERR_UNCONFIGURED;
    // Number of bytes accepted before sealing or failure.
    int64_t written = 0;
    // Whether finish has sealed the writer and released its cursor.
    bool sealed = false;

    // Allow writing only while a valid writer is open.
    bool can_write() const;
    // Write a scalar and retain the native writer's status.
    template <typename T> godot::Error write_value(T value, bool (WireWriter::*method)(T));
    // Check a Godot integer against the selected wire width before narrowing.
    template <typename T> godot::Error write_integer(int64_t value, bool (WireWriter::*method)(T));
    // Write a length-prefixed buffer within the configured byte budget.
    godot::Error write_blob(const uint8_t *data, size_t size);
    // Advance the accepted byte count or record a native capacity failure.
    godot::Error record_write(bool ok);

protected:
    // Expose the typed writer and its terminal operations to scripts.
    static void _bind_methods();

public:
    // Allocate a bounded writer. Inspect the returned writer's error before using it.
    static godot::Ref<NetworkWriter> create(int64_t max_bytes = default_codec_bytes);
    // Write a canonical one-byte Boolean.
    godot::Error write_bool(bool value);
    // Write an unsigned 8-bit integer.
    godot::Error write_u8(int64_t value);
    // Write an unsigned 16-bit integer.
    godot::Error write_u16(int64_t value);
    // Write an unsigned 32-bit integer.
    godot::Error write_u32(int64_t value);
    // Write a signed 8-bit integer.
    godot::Error write_i8(int64_t value);
    // Write a signed 16-bit integer.
    godot::Error write_i16(int64_t value);
    // Write a signed 32-bit integer.
    godot::Error write_i32(int64_t value);
    // Write a signed 64-bit integer.
    godot::Error write_i64(int64_t value);
    // Write a finite float rounded to IEEE-754 binary32 precision.
    godot::Error write_f32(double value);
    // Write a finite IEEE-754 binary64 float.
    godot::Error write_f64(double value);
    // Write a length-prefixed UTF-8 string within the remaining byte budget.
    godot::Error write_string(const godot::String &value);
    // Write a length-prefixed byte array within the remaining byte budget.
    godot::Error write_bytes(const godot::PackedByteArray &value);
    // Return the first writer error.
    godot::Error get_error() const;
    // Return the bytes accepted before the first failure, if any.
    int64_t get_size() const;
    // Seal the writer and return the complete payload, or empty bytes on error.
    godot::PackedByteArray finish();
};

// Reads typed fields from one bounded Godot byte buffer.
class NetworkReader : public godot::RefCounted {
    GDCLASS(NetworkReader, godot::RefCounted)

    // Retains a copy-on-write reference so the cursor outlives the caller's array.
    godot::PackedByteArray bytes;
    // Native decoder over the retained immutable storage.
    std::optional<WireReader> reader;
    // First reader error, or unconfigured until opened with bytes.
    godot::Error error = godot::ERR_UNCONFIGURED;

    // Read a scalar or return its neutral value and retain the first decode error.
    template <typename T> T read_value(bool (WireReader::*method)(T &));
    // Borrow a length-prefixed field from the underlying storage.
    bool read_blob(int64_t max_bytes, const uint8_t *&data, size_t &size);

protected:
    // Expose typed reads, validation, and status to scripts.
    static void _bind_methods();

public:
    // Open a message within the supplied byte budget and retain its storage.
    static godot::Ref<NetworkReader> open(const godot::PackedByteArray &bytes, int64_t max_bytes = default_codec_bytes);
    // Read a canonical one-byte Boolean.
    bool read_bool();
    // Read an unsigned 8-bit integer.
    int64_t read_u8();
    // Read an unsigned 16-bit integer.
    int64_t read_u16();
    // Read an unsigned 32-bit integer.
    int64_t read_u32();
    // Read a signed 8-bit integer.
    int64_t read_i8();
    // Read a signed 16-bit integer.
    int64_t read_i16();
    // Read a signed 32-bit integer.
    int64_t read_i32();
    // Read a signed 64-bit integer.
    int64_t read_i64();
    // Read a finite IEEE-754 binary32 float and widen it to a Godot float.
    double read_f32();
    // Read a finite IEEE-754 binary64 float.
    double read_f64();
    // Read a length-prefixed UTF-8 string with an optional per-field byte limit.
    godot::String read_string(int64_t max_bytes = max_codec_bytes);
    // Read a length-prefixed byte array with an optional per-field byte limit.
    godot::PackedByteArray read_bytes(int64_t max_bytes = max_codec_bytes);
    // Mark a decoded field as invalid for the selected message schema.
    void fail(godot::Error reason = godot::ERR_INVALID_DATA);
    // Return the first error without checking for trailing data.
    godot::Error get_error() const;
    // Return the number of bytes the reader has not consumed.
    int64_t get_remaining() const;
    // Require exact payload consumption and return the terminal decode status.
    godot::Error finish();
};

// Completed custom encode outcome with immutable status and payload access.
class NetworkEncodeResult : public godot::RefCounted {
    GDCLASS(NetworkEncodeResult, godot::RefCounted)
    friend class NetworkCodecs;

    // Terminal encoding status.
    godot::Error error = godot::ERR_UNCONFIGURED;
    // Complete binary payload on success.
    godot::PackedByteArray bytes;

protected:
    // Expose the read-only encode result to scripts.
    static void _bind_methods();

public:
    // Return whether the encoder produced a complete payload.
    bool is_ok() const;
    // Return the terminal encoder error.
    godot::Error get_error() const;
    // Return the complete payload on success, or empty bytes on failure.
    godot::PackedByteArray get_bytes() const;
};

// Completed custom decode outcome with an explicitly nullable payload.
class NetworkDecodeResult : public godot::RefCounted {
    GDCLASS(NetworkDecodeResult, godot::RefCounted)
    friend class NetworkCodecs;

    // Terminal decoding status.
    godot::Error error = godot::ERR_UNCONFIGURED;
    // Complete value on success, including a valid null payload.
    godot::Variant value;

protected:
    // Expose the read-only decode result to scripts.
    static void _bind_methods();

public:
    // Return whether the selected decoder consumed the entire message successfully.
    bool is_ok() const;
    // Return the terminal decoder error.
    godot::Error get_error() const;
    // Return the decoded value on success, or null on failure.
    godot::Variant get_value() const;
};

// Local adapters selected explicitly by game-owned stable identifier and version.
class NetworkCodecs : public godot::RefCounted {
    GDCLASS(NetworkCodecs, godot::RefCounted)

    // Game-authored callbacks for one message schema.
    struct Codec {
        // Writes a game value using the supplied writer, then returns a Godot error.
        godot::Callable encode;
        // Reconstructs a game value using the supplied reader.
        godot::Callable decode;
    };

    // Keys combine a local codec name and its exact schema version.
    using Key = std::pair<godot::StringName, int64_t>;
    // Registry is local to this object and does not resolve packet-supplied names.
    std::map<Key, Codec> codecs;
    // A scoped registry stops accepting work when its owner retires.
    bool closed = false;

protected:
    // Register the script-facing codec catalog and encode/decode operations.
    static void _bind_methods();

public:
    // Register one versioned pair of synchronous callbacks in this catalog.
    godot::Error register_codec(const godot::StringName &id, int64_t version,
            const godot::Callable &encoder, const godot::Callable &decoder);
    // Encode a game value using the explicitly selected local codec.
    godot::Ref<NetworkEncodeResult> encode(const godot::StringName &id, int64_t version,
            const godot::Variant &value, int64_t max_bytes = default_codec_bytes);
    // Decode a bounded message using the explicitly selected local codec.
    godot::Ref<NetworkDecodeResult> decode(const godot::StringName &id, int64_t version,
            const godot::PackedByteArray &bytes, int64_t max_bytes = default_codec_bytes);
    // Permanently retire a game-owned registry and release its callbacks.
    void close();
};

} // namespace crimson::replication
