// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace crimson::replication {

// Bounded little-endian writer over storage owned by the caller. The first failure is sticky.
class WireWriter {
    // Destination bytes remain valid for the writer's lifetime.
    uint8_t *buffer;
    // Size of the caller's destination buffer.
    size_t capacity;
    // Bytes successfully written so far.
    size_t cursor = 0;
    // Invalid arguments or exhausted capacity make subsequent writes fail.
    bool valid;

    // Write an unsigned value one byte at a time in wire order.
    template <typename UInt>
    bool write_unsigned(UInt value) {
        if (!reserve(sizeof(value))) {
            return false;
        }
        for (size_t index = 0; index < sizeof(value); ++index) {
            buffer[cursor++] = static_cast<uint8_t>(value >> (index * 8));
        }
        return true;
    }

    // Preserve the IEEE-754 bit representation without copying struct padding.
    template <typename UInt, typename Float>
    bool write_float(Float value) {
        static_assert(sizeof(UInt) == sizeof(Float) && std::numeric_limits<Float>::is_iec559);
        if (!std::isfinite(value)) {
            return fail();
        }
        UInt bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return write_unsigned(bits);
    }

    // Validate space before touching the destination.
    bool reserve(size_t count) { return valid && (count <= remaining() || fail()); }

public:
    // Accept null only for an empty destination.
    WireWriter(uint8_t *data, size_t length) : buffer(data), capacity(length), valid(data || length == 0) {}

    // Test whether all operations have succeeded.
    bool ok() const { return valid; }
    // Count successfully written bytes.
    size_t size() const { return cursor; }
    // Count bytes available in the caller's storage.
    size_t remaining() const { return capacity - cursor; }
    // Reject the current message and all further writes.
    bool fail() { valid = false; return false; }

    // Write fixed-width little-endian unsigned integers.
    bool write_u8(uint8_t value) { return write_unsigned(value); }
    bool write_u16(uint16_t value) { return write_unsigned(value); }
    bool write_u32(uint32_t value) { return write_unsigned(value); }
    bool write_u64(uint64_t value) { return write_unsigned(value); }
    // Write signed integers using their specified two's complement wire encoding.
    bool write_i8(int8_t value) { return write_u8(static_cast<uint8_t>(value)); }
    bool write_i16(int16_t value) { return write_u16(static_cast<uint16_t>(value)); }
    bool write_i32(int32_t value) { return write_u32(static_cast<uint32_t>(value)); }
    bool write_i64(int64_t value) { return write_u64(static_cast<uint64_t>(value)); }
    // Write a finite IEEE-754 value.
    bool write_f32(float value) { return write_float<uint32_t>(value); }
    bool write_f64(double value) { return write_float<uint64_t>(value); }
    // Write exactly one byte containing zero or one.
    bool write_bool(bool value) { return write_u8(value ? 1 : 0); }

    // Copy unframed bytes from a source that may overlap the destination.
    bool write_bytes(const uint8_t *data, size_t count) {
        if (!reserve(count) || (!data && count != 0)) {
            return fail();
        }
        if (count != 0) {
            std::memmove(buffer + cursor, data, count);
            cursor += count;
        }
        return true;
    }

    // Write a uint32 length and the full byte field, or leave the destination unchanged.
    bool write_blob(const uint8_t *data, size_t count) {
        if (count > std::numeric_limits<uint32_t>::max() || remaining() < sizeof(uint32_t) ||
                count > remaining() - sizeof(uint32_t) || (!data && count != 0) || !valid) {
            return fail();
        }
        if (count != 0) {
            std::memmove(buffer + cursor + sizeof(uint32_t), data, count);
        }
        write_u32(static_cast<uint32_t>(count));
        cursor += count;
        return true;
    }
};

// Bounded reader that borrows its source and never allocates. A failed field leaves its output unchanged.
class WireReader {
    // Source bytes remain valid for the reader's lifetime.
    const uint8_t *buffer;
    // Exact length of the supplied message.
    size_t length;
    // Bytes successfully consumed so far.
    size_t cursor = 0;
    // Malformed values and out-of-bounds access make further reads fail.
    bool valid;

    // Assemble an unsigned value in wire order, independent of host endianness.
    template <typename UInt>
    bool read_unsigned(UInt &value) {
        const uint8_t *bytes;
        if (!read_bytes(sizeof(value), bytes)) {
            return false;
        }
        uint64_t result = 0;
        for (size_t index = 0; index < sizeof(value); ++index) {
            result |= static_cast<uint64_t>(bytes[index]) << (index * 8);
        }
        value = static_cast<UInt>(result);
        return true;
    }

    // Decode two's complement without relying on implementation-defined narrowing to signed values.
    template <typename Int>
    bool read_signed(Int &value) {
        using UInt = std::make_unsigned_t<Int>;
        UInt bits;
        if (!read_unsigned(bits)) {
            return false;
        }
        if (bits <= static_cast<UInt>(std::numeric_limits<Int>::max())) {
            value = static_cast<Int>(bits);
        } else {
            value = static_cast<Int>(-1 - static_cast<Int>(static_cast<UInt>(~bits)));
        }
        return true;
    }

    // Reconstruct a finite IEEE-754 value and publish it only after validation.
    template <typename UInt, typename Float>
    bool read_float(Float &value) {
        static_assert(sizeof(UInt) == sizeof(Float) && std::numeric_limits<Float>::is_iec559);
        UInt bits;
        if (!read_unsigned(bits)) {
            return false;
        }
        Float decoded;
        std::memcpy(&decoded, &bits, sizeof(decoded));
        if (!std::isfinite(decoded)) {
            cursor -= sizeof(bits);
            return fail();
        }
        value = decoded;
        return true;
    }

public:
    // Accept null only for an empty source.
    WireReader(const uint8_t *data, size_t size) : buffer(data), length(size), valid(data || size == 0) {}

    // Test whether all operations have succeeded.
    bool ok() const { return valid; }
    // Count successfully consumed bytes.
    size_t size() const { return cursor; }
    // Count bytes still available.
    size_t remaining() const { return length - cursor; }
    // Accept only successful reads that consumed the entire message.
    bool finished() const { return valid && cursor == length; }
    // Reject the current message and all further reads.
    bool fail() { valid = false; return false; }

    // Read fixed-width little-endian unsigned integers.
    bool read_u8(uint8_t &value) { return read_unsigned(value); }
    bool read_u16(uint16_t &value) { return read_unsigned(value); }
    bool read_u32(uint32_t &value) { return read_unsigned(value); }
    bool read_u64(uint64_t &value) { return read_unsigned(value); }
    // Read fixed-width signed integers.
    bool read_i8(int8_t &value) { return read_signed(value); }
    bool read_i16(int16_t &value) { return read_signed(value); }
    bool read_i32(int32_t &value) { return read_signed(value); }
    bool read_i64(int64_t &value) { return read_signed(value); }
    // Read a finite IEEE-754 value.
    bool read_f32(float &value) { return read_float<uint32_t>(value); }
    bool read_f64(double &value) { return read_float<uint64_t>(value); }
    // Reject bool bytes outside the canonical zero or one encoding.
    bool read_bool(bool &value) {
        uint8_t raw;
        if (!read_u8(raw)) {
            return false;
        }
        if (raw > 1) {
            --cursor;
            return fail();
        }
        value = raw == 1;
        return true;
    }

    // Borrow exactly count bytes after checking bounds.
    bool read_bytes(size_t count, const uint8_t *&data) {
        if (!valid || count > remaining()) {
            return fail();
        }
        data = buffer ? buffer + cursor : nullptr;
        cursor += count;
        return true;
    }

    // Borrow a uint32-length field after checking both its field limit and the input bounds.
    bool read_blob(const uint8_t *&data, size_t &count, size_t limit) {
        const size_t start = cursor;
        uint32_t length_field;
        if (!read_u32(length_field) || length_field > limit || !read_bytes(length_field, data)) {
            cursor = start;
            return fail();
        }
        count = length_field;
        return true;
    }
};

} // namespace crimson::replication
