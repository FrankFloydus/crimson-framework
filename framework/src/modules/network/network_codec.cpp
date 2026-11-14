// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "network_codec.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <cstring>
#include <limits>

namespace crimson::replication {

using namespace godot;

namespace {

// Limit the catalog and its version keys without requiring global registrations.
constexpr size_t max_codecs = 256;
constexpr int64_t max_codec_version = 65535;
constexpr int64_t max_codec_name = 64;

// Allow only portable ASCII identifiers with a short, explicit schema version.
bool valid_key(const StringName &id, int64_t version) {
    const String name = id;
    if (name.is_empty() || name.length() > max_codec_name || version < 1 || version > max_codec_version) {
        return false;
    }
    for (int64_t index = 0; index < name.length(); ++index) {
        const char32_t code = name[index];
        if (!(code >= 'a' && code <= 'z') && !(code >= 'A' && code <= 'Z') &&
                !(code >= '0' && code <= '9') && code != '_' && code != '-' && code != '.') {
            return false;
        }
    }
    return true;
}

// Check canonical UTF-8 before passing untrusted bytes to Godot's string parser.
bool valid_utf8(const uint8_t *data, size_t length) {
    if (length >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        return false;
    }
    for (size_t index = 0; index < length;) {
        const uint8_t lead = data[index++];
        if (lead > 0 && lead <= 0x7f) {
            continue;
        }
        if (lead < 0xc2 || lead > 0xf4) {
            return false;
        }
        const size_t extra = lead <= 0xdf ? 1 : lead <= 0xef ? 2 : 3;
        if (extra > length - index) {
            return false;
        }
        const uint8_t second = data[index];
        if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)) {
            return false;
        }
        for (size_t end = index + extra; index < end; ++index) {
            if ((data[index] & 0xc0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

// Invoke a callable without treating an engine call error as a successful null value.
bool call_codec(const Callable &callback, const Variant **args, int count, Variant &result) {
    if (!callback.is_valid()) {
        return false;
    }
    Variant callable = callback;
    GDExtensionCallError call_error{};
    callable.callp(StringName("call"), args, count, result, call_error);
    return call_error.error == GDEXTENSION_CALL_OK;
}

} // namespace

void NetworkWriter::_bind_methods() {
    ClassDB::bind_static_method("NetworkWriter", D_METHOD("create", "max_bytes"), &NetworkWriter::create,
            DEFVAL(default_codec_bytes));
    ClassDB::bind_method(D_METHOD("write_bool", "value"), &NetworkWriter::write_bool);
    ClassDB::bind_method(D_METHOD("write_u8", "value"), &NetworkWriter::write_u8);
    ClassDB::bind_method(D_METHOD("write_u16", "value"), &NetworkWriter::write_u16);
    ClassDB::bind_method(D_METHOD("write_u32", "value"), &NetworkWriter::write_u32);
    ClassDB::bind_method(D_METHOD("write_i8", "value"), &NetworkWriter::write_i8);
    ClassDB::bind_method(D_METHOD("write_i16", "value"), &NetworkWriter::write_i16);
    ClassDB::bind_method(D_METHOD("write_i32", "value"), &NetworkWriter::write_i32);
    ClassDB::bind_method(D_METHOD("write_i64", "value"), &NetworkWriter::write_i64);
    ClassDB::bind_method(D_METHOD("write_f32", "value"), &NetworkWriter::write_f32);
    ClassDB::bind_method(D_METHOD("write_f64", "value"), &NetworkWriter::write_f64);
    ClassDB::bind_method(D_METHOD("write_string", "value"), &NetworkWriter::write_string);
    ClassDB::bind_method(D_METHOD("write_bytes", "value"), &NetworkWriter::write_bytes);
    ClassDB::bind_method(D_METHOD("get_error"), &NetworkWriter::get_error);
    ClassDB::bind_method(D_METHOD("get_size"), &NetworkWriter::get_size);
    ClassDB::bind_method(D_METHOD("finish"), &NetworkWriter::finish);
}

Ref<NetworkWriter> NetworkWriter::create(int64_t max_bytes) {
    Ref<NetworkWriter> result;
    result.instantiate();
    if (max_bytes < 0 || max_bytes > max_codec_bytes) {
        result->error = ERR_PARAMETER_RANGE_ERROR;
    } else if (result->bytes.resize(max_bytes) != OK) {
        result->error = ERR_OUT_OF_MEMORY;
    } else {
        result->writer.emplace(result->bytes.ptrw(), static_cast<size_t>(max_bytes));
        result->error = OK;
    }
    return result;
}

bool NetworkWriter::can_write() const { return error == OK && !sealed; }

Error NetworkWriter::record_write(bool ok) {
    if (!ok) {
        error = ERR_OUT_OF_MEMORY;
    } else {
        written = static_cast<int64_t>(writer->size());
    }
    return error;
}

template <typename T>
Error NetworkWriter::write_value(T value, bool (WireWriter::*method)(T)) {
    if (!can_write()) {
        return error == OK ? ERR_LOCKED : error;
    }
    return record_write((writer.value().*method)(value));
}

template <typename T>
Error NetworkWriter::write_integer(int64_t value, bool (WireWriter::*method)(T)) {
    if (!can_write()) {
        return error == OK ? ERR_LOCKED : error;
    }
    if (value < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
            value > static_cast<int64_t>(std::numeric_limits<T>::max())) {
        return error = ERR_PARAMETER_RANGE_ERROR;
    }
    return write_value(static_cast<T>(value), method);
}

Error NetworkWriter::write_bool(bool value) { return write_value(value, &WireWriter::write_bool); }
Error NetworkWriter::write_u8(int64_t value) { return write_integer(value, &WireWriter::write_u8); }
Error NetworkWriter::write_u16(int64_t value) { return write_integer(value, &WireWriter::write_u16); }
Error NetworkWriter::write_u32(int64_t value) { return write_integer(value, &WireWriter::write_u32); }
Error NetworkWriter::write_i8(int64_t value) { return write_integer(value, &WireWriter::write_i8); }
Error NetworkWriter::write_i16(int64_t value) { return write_integer(value, &WireWriter::write_i16); }
Error NetworkWriter::write_i32(int64_t value) { return write_integer(value, &WireWriter::write_i32); }
Error NetworkWriter::write_i64(int64_t value) { return write_value(value, &WireWriter::write_i64); }

Error NetworkWriter::write_f32(double value) {
    if (!can_write()) {
        return error == OK ? ERR_LOCKED : error;
    }
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        return error = ERR_PARAMETER_RANGE_ERROR;
    }
    return write_value(static_cast<float>(value), &WireWriter::write_f32);
}

Error NetworkWriter::write_f64(double value) {
    if (!can_write()) {
        return error == OK ? ERR_LOCKED : error;
    }
    if (!std::isfinite(value)) {
        return error = ERR_PARAMETER_RANGE_ERROR;
    }
    return write_value(value, &WireWriter::write_f64);
}

Error NetworkWriter::write_blob(const uint8_t *data, size_t size) {
    if (!can_write()) {
        return error == OK ? ERR_LOCKED : error;
    }
    return record_write(writer->write_blob(data, size));
}

Error NetworkWriter::write_string(const String &value) {
    if (!can_write()) {
        return error == OK ? ERR_LOCKED : error;
    }
    if (writer->remaining() < sizeof(uint32_t) ||
            static_cast<size_t>(value.length()) > writer->remaining() - sizeof(uint32_t)) {
        return error = ERR_OUT_OF_MEMORY;
    }
    if (!value.is_empty() && value[0] == 0xfeff) {
        return error = ERR_INVALID_DATA;
    }
    size_t encoded_size = 0;
    for (int64_t index = 0; index < value.length(); ++index) {
        const char32_t code = value[index];
        if (code == 0 || (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) {
            return error = ERR_INVALID_DATA;
        }
        encoded_size += code <= 0x7f ? 1 : code <= 0x7ff ? 2 : code <= 0xffff ? 3 : 4;
    }
    if (encoded_size > writer->remaining() - sizeof(uint32_t)) {
        return error = ERR_OUT_OF_MEMORY;
    }
    const CharString utf8 = value.utf8();
    return write_blob(reinterpret_cast<const uint8_t *>(utf8.get_data()), static_cast<size_t>(utf8.length()));
}

Error NetworkWriter::write_bytes(const PackedByteArray &value) {
    return write_blob(value.ptr(), static_cast<size_t>(value.size()));
}

Error NetworkWriter::get_error() const { return error; }
int64_t NetworkWriter::get_size() const { return written; }

PackedByteArray NetworkWriter::finish() {
    if (!sealed) {
        sealed = true;
        writer.reset();
        if (error == OK && bytes.resize(written) != OK) {
            error = ERR_OUT_OF_MEMORY;
        }
        if (error != OK) {
            bytes.clear();
        }
    }
    return bytes;
}

void NetworkReader::_bind_methods() {
    ClassDB::bind_static_method("NetworkReader", D_METHOD("open", "bytes", "max_bytes"), &NetworkReader::open,
            DEFVAL(default_codec_bytes));
    ClassDB::bind_method(D_METHOD("read_bool"), &NetworkReader::read_bool);
    ClassDB::bind_method(D_METHOD("read_u8"), &NetworkReader::read_u8);
    ClassDB::bind_method(D_METHOD("read_u16"), &NetworkReader::read_u16);
    ClassDB::bind_method(D_METHOD("read_u32"), &NetworkReader::read_u32);
    ClassDB::bind_method(D_METHOD("read_i8"), &NetworkReader::read_i8);
    ClassDB::bind_method(D_METHOD("read_i16"), &NetworkReader::read_i16);
    ClassDB::bind_method(D_METHOD("read_i32"), &NetworkReader::read_i32);
    ClassDB::bind_method(D_METHOD("read_i64"), &NetworkReader::read_i64);
    ClassDB::bind_method(D_METHOD("read_f32"), &NetworkReader::read_f32);
    ClassDB::bind_method(D_METHOD("read_f64"), &NetworkReader::read_f64);
    ClassDB::bind_method(D_METHOD("read_string", "max_bytes"), &NetworkReader::read_string, DEFVAL(max_codec_bytes));
    ClassDB::bind_method(D_METHOD("read_bytes", "max_bytes"), &NetworkReader::read_bytes, DEFVAL(max_codec_bytes));
    ClassDB::bind_method(D_METHOD("fail", "reason"), &NetworkReader::fail, DEFVAL(ERR_INVALID_DATA));
    ClassDB::bind_method(D_METHOD("get_error"), &NetworkReader::get_error);
    ClassDB::bind_method(D_METHOD("get_remaining"), &NetworkReader::get_remaining);
    ClassDB::bind_method(D_METHOD("finish"), &NetworkReader::finish);
}

Ref<NetworkReader> NetworkReader::open(const PackedByteArray &input, int64_t max_bytes) {
    Ref<NetworkReader> result;
    result.instantiate();
    if (max_bytes < 0 || max_bytes > max_codec_bytes) {
        result->error = ERR_PARAMETER_RANGE_ERROR;
    } else if (input.size() > max_bytes) {
        result->error = ERR_INVALID_DATA;
    } else {
        result->bytes = input;
        result->reader.emplace(result->bytes.ptr(), static_cast<size_t>(input.size()));
        result->error = OK;
    }
    return result;
}

template <typename T>
T NetworkReader::read_value(bool (WireReader::*method)(T &)) {
    T value{};
    if (error == OK && !(reader.value().*method)(value)) {
        fail();
    }
    return value;
}

bool NetworkReader::read_bool() { return read_value(&WireReader::read_bool); }
int64_t NetworkReader::read_u8() { return read_value(&WireReader::read_u8); }
int64_t NetworkReader::read_u16() { return read_value(&WireReader::read_u16); }
int64_t NetworkReader::read_u32() { return read_value(&WireReader::read_u32); }
int64_t NetworkReader::read_i8() { return read_value(&WireReader::read_i8); }
int64_t NetworkReader::read_i16() { return read_value(&WireReader::read_i16); }
int64_t NetworkReader::read_i32() { return read_value(&WireReader::read_i32); }
int64_t NetworkReader::read_i64() { return read_value(&WireReader::read_i64); }
double NetworkReader::read_f32() { return read_value(&WireReader::read_f32); }
double NetworkReader::read_f64() { return read_value(&WireReader::read_f64); }

bool NetworkReader::read_blob(int64_t max_bytes, const uint8_t *&data, size_t &size) {
    if (error != OK) {
        return false;
    }
    if (max_bytes < 0 || max_bytes > max_codec_bytes) {
        fail(ERR_PARAMETER_RANGE_ERROR);
        return false;
    }
    if (!reader->read_blob(data, size, static_cast<size_t>(max_bytes))) {
        fail();
        return false;
    }
    return true;
}

String NetworkReader::read_string(int64_t max_bytes) {
    const uint8_t *data;
    size_t size;
    if (!read_blob(max_bytes, data, size) || size == 0) {
        return {};
    }
    String text;
    if (!valid_utf8(data, size) ||
            text.parse_utf8(reinterpret_cast<const char *>(data), static_cast<int64_t>(size)) != OK) {
        fail();
        return {};
    }
    return text;
}

PackedByteArray NetworkReader::read_bytes(int64_t max_bytes) {
    const uint8_t *data;
    size_t size;
    if (!read_blob(max_bytes, data, size) || size == 0) {
        return {};
    }
    PackedByteArray output;
    if (output.resize(static_cast<int64_t>(size)) != OK) {
        fail(ERR_OUT_OF_MEMORY);
        return {};
    }
    std::memcpy(output.ptrw(), data, size);
    return output;
}

void NetworkReader::fail(Error reason) {
    if (error == OK) {
        error = reason == OK ? ERR_INVALID_DATA : reason;
        reader->fail();
    }
}

Error NetworkReader::get_error() const { return error; }
int64_t NetworkReader::get_remaining() const { return reader ? static_cast<int64_t>(reader->remaining()) : 0; }

Error NetworkReader::finish() {
    if (error == OK && !reader->finished()) {
        fail();
    }
    return error;
}

void NetworkEncodeResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ok"), &NetworkEncodeResult::is_ok);
    ClassDB::bind_method(D_METHOD("get_error"), &NetworkEncodeResult::get_error);
    ClassDB::bind_method(D_METHOD("get_bytes"), &NetworkEncodeResult::get_bytes);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ok"), "", "is_ok");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "error"), "", "get_error");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes"), "", "get_bytes");
}

bool NetworkEncodeResult::is_ok() const { return error == OK; }
Error NetworkEncodeResult::get_error() const { return error; }
PackedByteArray NetworkEncodeResult::get_bytes() const { return bytes; }

void NetworkDecodeResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ok"), &NetworkDecodeResult::is_ok);
    ClassDB::bind_method(D_METHOD("get_error"), &NetworkDecodeResult::get_error);
    ClassDB::bind_method(D_METHOD("get_value"), &NetworkDecodeResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ok"), "", "is_ok");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "error"), "", "get_error");
    ADD_PROPERTY(PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_value");
}

bool NetworkDecodeResult::is_ok() const { return error == OK; }
Error NetworkDecodeResult::get_error() const { return error; }
Variant NetworkDecodeResult::get_value() const { return value; }

void NetworkCodecs::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_codec", "id", "version", "encode", "decode"), &NetworkCodecs::register_codec);
    ClassDB::bind_method(D_METHOD("encode", "id", "version", "value", "max_bytes"), &NetworkCodecs::encode,
            DEFVAL(default_codec_bytes));
    ClassDB::bind_method(D_METHOD("decode", "id", "version", "bytes", "max_bytes"), &NetworkCodecs::decode,
            DEFVAL(default_codec_bytes));
}

Error NetworkCodecs::register_codec(const StringName &id, int64_t version, const Callable &encoder,
        const Callable &decoder) {
    if (closed) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_key(id, version) || !encoder.is_valid() || !decoder.is_valid() ||
            encoder.get_argument_count() != 2 || decoder.get_argument_count() != 1) {
        return ERR_INVALID_PARAMETER;
    }
    const Key key{id, version};
    if (codecs.find(key) != codecs.end()) {
        return ERR_ALREADY_EXISTS;
    }
    if (codecs.size() >= max_codecs) {
        return ERR_OUT_OF_MEMORY;
    }
    codecs.emplace(key, Codec{encoder, decoder});
    return OK;
}

Ref<NetworkEncodeResult> NetworkCodecs::encode(const StringName &id, int64_t version, const Variant &value,
        int64_t max_bytes) {
    Ref<NetworkEncodeResult> result;
    result.instantiate();
    if (closed) {
        result->error = ERR_UNAVAILABLE;
        return result;
    }
    const auto found = codecs.find(Key{id, version});
    if (found == codecs.end()) {
        result->error = ERR_DOES_NOT_EXIST;
        return result;
    }
    const Ref<NetworkWriter> writer = NetworkWriter::create(max_bytes);
    if (writer->get_error() != OK) {
        result->error = writer->get_error();
        return result;
    }
    const Callable callback = found->second.encode;
    const Variant writer_value = writer;
    const Variant *args[] = {&writer_value, &value};
    Variant outcome;
    const bool called = call_codec(callback, args, 2, outcome);
    if (writer->get_error() != OK) {
        result->error = writer->get_error();
        return result;
    }
    if (!called || outcome.get_type() != Variant::INT) {
        result->error = ERR_INVALID_DECLARATION;
        return result;
    }
    const int64_t code = outcome;
    if (code < OK || code > ERR_PRINTER_ON_FIRE) {
        result->error = ERR_INVALID_DECLARATION;
        return result;
    }
    if (closed || code != OK) {
        result->error = closed ? ERR_UNAVAILABLE : static_cast<Error>(code);
        return result;
    }
    result->bytes = writer->finish();
    result->error = writer->get_error();
    return result;
}

Ref<NetworkDecodeResult> NetworkCodecs::decode(const StringName &id, int64_t version, const PackedByteArray &bytes,
        int64_t max_bytes) {
    Ref<NetworkDecodeResult> result;
    result.instantiate();
    if (closed) {
        result->error = ERR_UNAVAILABLE;
        return result;
    }
    const auto found = codecs.find(Key{id, version});
    if (found == codecs.end()) {
        result->error = ERR_DOES_NOT_EXIST;
        return result;
    }
    const Ref<NetworkReader> reader = NetworkReader::open(bytes, max_bytes);
    if (reader->get_error() != OK) {
        result->error = reader->get_error();
        return result;
    }
    const Callable callback = found->second.decode;
    const Variant reader_value = reader;
    const Variant *args[] = {&reader_value};
    Variant value;
    const bool called = call_codec(callback, args, 1, value);
    if (reader->get_error() != OK) {
        result->error = reader->get_error();
        return result;
    }
    if (!called) {
        result->error = ERR_INVALID_DECLARATION;
        return result;
    }
    result->error = closed ? ERR_UNAVAILABLE : reader->finish();
    if (result->error == OK) {
        result->value = value;
    }
    return result;
}

void NetworkCodecs::close() {
    closed = true;
    codecs.clear();
}

} // namespace crimson::replication
