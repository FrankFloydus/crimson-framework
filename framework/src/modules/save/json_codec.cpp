// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "json_codec_payload.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ostream>
#include <streambuf>
#include <string_view>
#include <utility>

namespace checkpoint {
namespace {

constexpr std::array<std::uint8_t, 4> magic{'G', 'S', 'A', 'V'};
constexpr std::uint16_t frame_version = 1;
constexpr std::uint16_t json_codec_id = 1;
constexpr std::size_t header_size = 16;
constexpr std::size_t digest_size = 32;
constexpr std::size_t framing_size = header_size + digest_size;
constexpr std::size_t hard_depth_limit = 128;
constexpr std::int64_t max_exact_integer = 9'007'199'254'740'991;

// Read a little-endian integer from the frame.
template <class Integer>
Integer read_le(const Bytes &bytes, std::size_t offset) {
	Integer value = 0;
	for (std::size_t index = 0; index < sizeof(Integer); ++index) {
		value = static_cast<Integer>(value | (static_cast<Integer>(bytes[offset + index]) << (8 * index)));
	}
	return value;
}

// Write a little-endian integer to the frame.
template <class Integer>
void write_le(Bytes &bytes, std::size_t offset, Integer value) {
	for (std::size_t index = 0; index < sizeof(Integer); ++index) {
		bytes[offset + index] = static_cast<std::uint8_t>(value >> (8 * index));
	}
}

// Compute the SHA-256 digest of the supplied bytes.
std::array<std::uint8_t, digest_size> sha256(const Bytes &bytes, std::size_t count) {
	if (count > std::numeric_limits<ULONG>::max()) {
		throw SaveError("LimitExceeded", "Save bytes exceed the SHA-256 provider input limit.");
	}
	std::array<std::uint8_t, digest_size> digest{};
	// The Windows API accepts a mutable pointer but reads the input without modifying it.
	const NTSTATUS status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
			const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(count),
			digest.data(), static_cast<ULONG>(digest.size()));
	if (status < 0) {
		throw SaveError("IoError", "Windows could not compute the save digest.");
	}
	return digest;
}

// Check whether a string is well-formed UTF-8.
bool is_utf8(std::string_view value) {
	if (value.empty()) {
		return true;
	}
	if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		return false;
	}
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				   value.data(), static_cast<int>(value.size()), nullptr, 0) != 0;
}

// Reject strings and keys that exceed the configured byte limit.
void check_string_size(std::string_view value, const Limits &limits) {
	if (value.size() > limits.max_string_bytes) {
		throw SaveError("LimitExceeded", "A JSON string exceeds the UTF-8 byte limit.");
	}
}

// Tracks node count and nesting depth during JSON processing.
class JsonBudget {
public:
	// Create a budget from the checkpoint limits.
	explicit JsonBudget(const Limits &limits) :
			limits_(limits) {}

	// Charge one value and enforce the depth and node limits.
	void consume(std::size_t depth) {
		if (depth >= std::min(limits_.max_depth, hard_depth_limit)) {
			throw SaveError("LimitExceeded", "The JSON nesting depth exceeds the limit.");
		}
		if (nodes_ >= limits_.max_nodes) {
			throw SaveError("LimitExceeded", "The JSON value count exceeds the limit.");
		}
		++nodes_;
	}

private:
	// Configured depth and node limits for validation.
	const Limits &limits_;
	// Count of JSON values processed so far.
	std::size_t nodes_ = 0;
};

// Reject signed integers outside the exact JSON integer range.
void check_signed(std::int64_t value, const char *error_code) {
	if (value < -max_exact_integer || value > max_exact_integer) {
		throw SaveError(error_code, "An integer exceeds the exact JSON range. Use a decimal string.");
	}
}

// Reject unsigned integers outside the exact JSON integer range.
void check_unsigned(std::uint64_t value, const char *error_code) {
	if (value > static_cast<std::uint64_t>(max_exact_integer)) {
		throw SaveError(error_code, "An integer exceeds the exact JSON range. Use a decimal string.");
	}
}

// Reject numeric values that cannot be represented as finite checkpoint JSON.
void check_finite(double value, const char *error_code) {
	if (!std::isfinite(value)) {
		throw SaveError(error_code, "JSON numbers must be finite.");
	}
}

// Reject embedded NUL bytes in checkpoint JSON strings.
void check_nul(std::string_view value, const char *error_code) {
	if (value.find('\0') != std::string_view::npos) {
		throw SaveError(error_code, "Save strings cannot contain NUL characters.");
	}
}

// Validate a checkpoint string's size, UTF-8 encoding, and NUL restrictions.
void validate_string(std::string_view value, const Limits &limits) {
	check_string_size(value, limits);
	check_nul(value, "InvalidData");
	if (!is_utf8(value)) {
		throw SaveError("InvalidData", "A JSON string contains invalid UTF-8.");
	}
}

// Recursively check JSON values and charge the shared depth and node budget.
void validate_value(const Json &value, const Limits &limits, JsonBudget &budget, std::size_t depth) {
	budget.consume(depth);
	switch (value.type()) {
		case Json::value_t::object:
			for (auto member = value.begin(); member != value.end(); ++member) {
				validate_string(member.key(), limits);
				validate_value(member.value(), limits, budget, depth + 1);
			}
			return;
		case Json::value_t::array:
			for (const auto &element : value) {
				validate_value(element, limits, budget, depth + 1);
			}
			return;
		case Json::value_t::string:
			validate_string(value.get_ref<const Json::string_t &>(), limits);
			return;
		case Json::value_t::number_integer:
			check_signed(value.get<std::int64_t>(), "InvalidData");
			return;
		case Json::value_t::number_unsigned:
			check_unsigned(value.get<std::uint64_t>(), "InvalidData");
			return;
		case Json::value_t::number_float:
			check_finite(value.get<double>(), "InvalidData");
			return;
		case Json::value_t::boolean:
		case Json::value_t::null:
			return;
		default:
			throw SaveError("InvalidData", "Unsupported value in the JSON data tree.");
	}
}

// Stream buffer that appends JSON bytes within a fixed output budget.
class BoundedJsonOutput : public std::streambuf {
public:
	// Associate the stream buffer with a byte vector and maximum size.
	BoundedJsonOutput(Bytes &output, std::size_t max_size) :
			output_(output), max_size_(max_size) {}

protected:
	// Append a contiguous chunk after checking the remaining capacity.
	std::streamsize xsputn(const char *data, std::streamsize count) override {
		const auto size = static_cast<std::size_t>(count);
		check_capacity(size);
		const auto *begin = reinterpret_cast<const std::uint8_t *>(data);
		output_.insert(output_.end(), begin, begin + size);
		return count;
	}

	// Append a single byte if it fits within the output budget.
	int_type overflow(int_type value) override {
		if (traits_type::eq_int_type(value, traits_type::eof())) {
			return traits_type::not_eof(value);
		}
		check_capacity(1);
		output_.push_back(static_cast<std::uint8_t>(traits_type::to_char_type(value)));
		return value;
	}

private:
	// Reject a write that would exceed the output size limit.
	void check_capacity(std::size_t count) const {
		if (count > max_size_ - output_.size()) {
			throw SaveError("LimitExceeded", "The encoded save exceeds the file size limit.");
		}
	}

	// Byte vector receiving serialized JSON.
	Bytes &output_;
	// Maximum total output size including bytes already present.
	std::size_t max_size_;
};

// SAX reader that validates JSON as it builds a detached checkpoint tree.
class StrictJsonReader {
public:
	// Initialize the reader's shared value and string limits.
	explicit StrictJsonReader(const Limits &limits) :
			limits_(limits), budget_(limits) {}

	// Append a JSON null value.
	bool null() {
		append(nullptr);
		return true;
	}

	// Append a JSON boolean.
	bool boolean(bool value) {
		append(value);
		return true;
	}

	// Validate and append a signed integer.
	bool number_integer(Json::number_integer_t value) {
		check_signed(value, "Corrupt");
		append(value);
		return true;
	}

	// Validate and append an unsigned integer.
	bool number_unsigned(Json::number_unsigned_t value) {
		check_unsigned(value, "Corrupt");
		append(value);
		return true;
	}

	// Append a finite fractional value and reject overflowed integer tokens.
	bool number_float(Json::number_float_t value, const Json::string_t &token) {
		check_finite(value, "Corrupt");
		// The lexer reports integer literals beyond its integer storage range as floats.
		if (token.find_first_of(".eE") == Json::string_t::npos) {
			throw SaveError("Corrupt", "An integer exceeds the exact JSON range.");
		}
		append(value);
		return true;
	}

	// Validate a text value before adding it to the tree.
	bool string(Json::string_t &value) {
		check_string_size(value, limits_);
		check_nul(value, "Corrupt");
		append(std::move(value));
		return true;
	}

	// Reject binary values in a JSON payload.
	bool binary(Json::binary_t &) {
		return false;
	}

	// Append an object and mark it as open for incoming members.
	bool start_object(std::size_t) {
		Json &value = append(Json::object());
		open_.push_back({ &value, {} });
		return true;
	}

	// Validate the next object key and reject duplicate members.
	bool key(Json::string_t &value) {
		check_string_size(value, limits_);
		check_nul(value, "Corrupt");
		auto &container = open_.back();
		if (container.value->contains(value)) {
			throw SaveError("Corrupt", "The JSON payload contains a duplicate object key.");
		}
		container.key = std::move(value);
		return true;
	}

	// Close the current object.
	bool end_object() {
		open_.pop_back();
		return true;
	}

	// Append an array and mark it as open for incoming elements.
	bool start_array(std::size_t) {
		Json &value = append(Json::array());
		open_.push_back({ &value, {} });
		return true;
	}

	// Close the current array.
	bool end_array() {
		open_.pop_back();
		return true;
	}

	// Tell the JSON parser to stop after a syntax error.
	bool parse_error(std::size_t, const std::string &, const Json::exception &) {
		return false;
	}

	// Move the finished tree out of the reader.
	Json take_root() {
		return std::move(root_);
	}

private:
	// SAX parser state for an object or array receiving values.
	struct OpenContainer {
		// Container in the result tree receiving incoming values.
		Json *value;
		// Pending member key when the open container is an object.
		std::string key;
	};

	// Charge and insert a value into the current container or at the root.
	Json &append(Json value) {
		budget_.consume(open_.size());
		if (open_.empty()) {
			root_ = std::move(value);
			return root_;
		}
		auto &parent = open_.back();
		if (parent.value->is_array()) {
			return parent.value->emplace_back(std::move(value));
		}
		auto &object = parent.value->get_ref<Json::object_t &>();
		return object.try_emplace(std::move(parent.key), std::move(value)).first->second;
	}

	// String limits applied to text values and object keys.
	const Limits &limits_;
	// Shared value count and nesting limits for the parse.
	JsonBudget budget_;
	// Root of the detached JSON tree being built.
	Json root_;
	// Stack of containers currently open in the JSON input.
	std::vector<OpenContainer> open_;
};

// Serialize validated JSON through a bounded byte stream.
void append_payload(Bytes &bytes, const Json &value, std::size_t max_size) {
	try {
		BoundedJsonOutput buffer(bytes, max_size);
		std::ostream stream(&buffer);
		stream.exceptions(std::ios::badbit | std::ios::failbit);
		stream << value;
	} catch (const Json::exception &) {
		throw SaveError("InvalidData", "The snapshot could not be encoded as JSON.");
	} catch (const std::ios_base::failure &) {
		throw SaveError("IoError", "The JSON output stream failed.");
	}
}

// Reject forbidden framing bytes and parse a strict JSON payload.
Json decode_payload(Bytes::const_iterator payload_begin, Bytes::const_iterator payload_end,
		const Limits &limits) {
	constexpr std::array<std::uint8_t, 3> utf8_bom{ 0xef, 0xbb, 0xbf };
	// The JSON library accepts a BOM and treats raw NUL as end-of-input.
	if ((payload_end - payload_begin >= static_cast<std::ptrdiff_t>(utf8_bom.size()) &&
				std::equal(utf8_bom.begin(), utf8_bom.end(), payload_begin)) ||
			std::find(payload_begin, payload_end, 0) != payload_end) {
		throw SaveError("Corrupt", "The JSON payload contains forbidden framing bytes.");
	}

	StrictJsonReader reader(limits);
	try {
		if (!Json::sax_parse(payload_begin, payload_end, &reader)) {
			throw SaveError("Corrupt", "The save payload is malformed JSON.");
		}
	} catch (const Json::exception &) {
		throw SaveError("Corrupt", "The save payload is malformed JSON.");
	}
	return reader.take_root();
}

// Built-in codec for GSAV frames containing a JSON payload and digest.
class FramedJsonCodec final : public Codec {
public:
	// Build a size-checked GSAV frame with a SHA-256 digest.
	Bytes encode(const Json &value, const Limits &limits) override {
		if (limits.max_file_bytes < framing_size) {
			throw SaveError("LimitExceeded", "The file limit is smaller than the save frame.");
		}
		validate_json(value, limits);
		Bytes bytes(header_size);
		std::copy(magic.begin(), magic.end(), bytes.begin());
		write_le(bytes, 4, frame_version);
		write_le(bytes, 6, json_codec_id);

		append_payload(bytes, value, limits.max_file_bytes - digest_size);

		write_le(bytes, 8, static_cast<std::uint64_t>(bytes.size() - header_size));
		const auto digest = sha256(bytes, bytes.size());
		bytes.insert(bytes.end(), digest.begin(), digest.end());
		return bytes;
	}

	// Verify the GSAV frame, digest, and strict JSON payload.
	Json decode(const Bytes &bytes, const Limits &limits) override {
		if (bytes.size() > limits.max_file_bytes) {
			throw SaveError("LimitExceeded", "The save file exceeds the file size limit.");
		}
		if (bytes.size() < framing_size || !std::equal(magic.begin(), magic.end(), bytes.begin())) {
			throw SaveError("Corrupt", "The save file header is missing or invalid.");
		}
		if (read_le<std::uint64_t>(bytes, 8) != bytes.size() - framing_size) {
			throw SaveError("Corrupt", "The save payload length does not match the frame.");
		}
		const auto digest_begin = bytes.end() - digest_size;
		const auto digest = sha256(bytes, bytes.size() - digest_size);
		if (!std::equal(digest.begin(), digest.end(), digest_begin)) {
			throw SaveError("Corrupt", "The save digest does not match the stored bytes.");
		}
		if (read_le<std::uint16_t>(bytes, 4) != frame_version ||
				read_le<std::uint16_t>(bytes, 6) != json_codec_id) {
			throw SaveError("Incompatible", "The save uses an unsupported frame or codec.");
		}

		return decode_payload(bytes.begin() + header_size, digest_begin, limits);
	}
};

} // namespace

Bytes encode_json_payload(const Json &value, const Limits &limits) {
	validate_json(value, limits);
	Bytes bytes;
	append_payload(bytes, value, limits.max_file_bytes);
	return bytes;
}

Json decode_json_payload(const Bytes &bytes, const Limits &limits) {
	if (bytes.size() > limits.max_file_bytes) {
		throw SaveError("LimitExceeded", "The JSON payload exceeds the file size limit.");
	}
	return decode_payload(bytes.begin(), bytes.end(), limits);
}

std::shared_ptr<Codec> make_json_codec() {
	return std::make_shared<FramedJsonCodec>();
}

void validate_json(const Json &value, const Limits &limits) {
	JsonBudget budget(limits);
	validate_value(value, limits, budget, 0);
}

} // namespace checkpoint
