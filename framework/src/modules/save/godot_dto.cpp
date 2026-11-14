// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "godot_dto.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace checkpoint {
namespace {

constexpr std::int64_t max_exact_integer = 9'007'199'254'740'991;
constexpr std::size_t hard_depth_limit = 128;

// Reject integers outside the exact checkpoint JSON range.
void check_exact_integer(std::int64_t value) {
    if (value < -max_exact_integer || value > max_exact_integer) {
        throw SaveError("InvalidData", "An integer exceeds the exact JSON range. Use a decimal string.");
    }
}

// Reject numeric values that cannot be represented as finite checkpoint JSON.
void check_finite(double value) {
    if (!std::isfinite(value)) {
        throw SaveError("InvalidData", "Checkpoint numbers must be finite.");
    }
}

// Reject embedded NUL bytes in Godot checkpoint text.
void check_embedded_nul(const std::string &value) {
    if (value.find('\0') != std::string::npos) {
        throw SaveError("InvalidData", "Godot checkpoint strings cannot contain embedded NUL characters.");
    }
}

// Converts Godot DTOs into JSON while enforcing aggregate snapshot budgets.
class SnapshotConverter {
public:
    // Initialize byte and node budgets for a detached snapshot.
    explicit SnapshotConverter(const Limits &limits) :
            limits_(limits), bytes_left_(limits.max_file_bytes), nodes_left_(limits.max_nodes) {}

    // Convert one value at its current nesting depth and charge its resource cost.
    Json convert(const godot::Variant &value, std::size_t depth = 0) {
        if (depth >= std::min(limits_.max_depth, hard_depth_limit)) {
            throw SaveError("LimitExceeded", "Checkpoint data exceeds the nesting limit.");
        }
        check_children(1);
        --nodes_left_;

        switch (value.get_type()) {
            case godot::Variant::NIL:
                consume_bytes(4);
                return nullptr;
            case godot::Variant::BOOL: {
                const bool boolean = static_cast<bool>(value);
                consume_bytes(boolean ? 4 : 5);
                return boolean;
            }
            case godot::Variant::INT: {
                const auto integer = static_cast<std::int64_t>(value);
                check_exact_integer(integer);
                consume_bytes(std::to_string(integer).size());
                return integer;
            }
            case godot::Variant::FLOAT: {
                const auto number = static_cast<double>(value);
                check_finite(number);
                consume_bytes(25);
                return number;
            }
            case godot::Variant::STRING:
                return convert_string(static_cast<godot::String>(value));
            case godot::Variant::ARRAY:
                return convert_array(static_cast<godot::Array>(value), depth);
            case godot::Variant::DICTIONARY:
                return convert_dictionary(static_cast<godot::Dictionary>(value), depth);
            default:
                throw SaveError("InvalidData", "Checkpoint data must contain JSON-compatible Godot values.");
        }
    }

private:
    // Configured depth, string, byte, and node limits for conversion.
    const Limits &limits_;
    // Remaining estimated bytes for the serialized snapshot.
    std::size_t bytes_left_;
    // Remaining value count in the snapshot.
    std::size_t nodes_left_;

    // Deduct bytes from the snapshot budget or report exhaustion.
    void consume_bytes(std::size_t count) {
        if (count > bytes_left_) {
            throw SaveError("LimitExceeded", "Checkpoint data exceeds the snapshot byte budget.");
        }
        bytes_left_ -= count;
    }

    // Check that a container can fit its immediate children within the remaining value budget.
    void check_children(std::size_t count) const {
        if (count > nodes_left_) {
            throw SaveError("LimitExceeded", "Checkpoint data exceeds the value count limit.");
        }
    }

    // Convert text to UTF-8 and account for JSON quotes and escapes.
    std::string convert_string(const godot::String &value) {
        const auto characters = static_cast<std::size_t>(value.length());
        if (characters > limits_.max_string_bytes || characters > bytes_left_) {
            throw SaveError("LimitExceeded", "A checkpoint string exceeds the UTF-8 byte budget.");
        }

        std::string text = utf8(value);
        if (text.size() > limits_.max_string_bytes) {
            throw SaveError("LimitExceeded", "A checkpoint string exceeds the UTF-8 byte limit.");
        }
        consume_bytes(2);
        consume_bytes(text.size());
        for (unsigned char byte : text) {
            if (byte < 0x20) {
                consume_bytes(5);
            } else if (byte == '"' || byte == '\\') {
                consume_bytes(1);
            }
        }
        return text;
    }

    // Convert array elements recursively under the shared budget.
    Json convert_array(const godot::Array &array, std::size_t depth) {
        const std::int64_t count = array.size();
        check_children(static_cast<std::size_t>(count));
        consume_bytes(2);
        Json result = Json::array();
        for (std::int64_t index = 0; index < count; ++index) {
            if (index > 0) {
                consume_bytes(1);
            }
            result.push_back(convert(array[index], depth + 1));
        }
        return result;
    }

    // Convert String-keyed entries and reject UTF-8 key collisions.
    Json convert_dictionary(const godot::Dictionary &dictionary, std::size_t depth) {
        check_children(static_cast<std::size_t>(dictionary.size()));
        consume_bytes(2);
        const godot::Array keys = dictionary.keys();
        Json result = Json::object();
        for (std::int64_t index = 0; index < keys.size(); ++index) {
            const godot::Variant &key = keys[index];
            if (key.get_type() != godot::Variant::STRING) {
                throw SaveError("InvalidData", "Checkpoint dictionaries require String keys.");
            }

            consume_bytes(index == 0 ? 1 : 2);
            std::string name = convert_string(static_cast<godot::String>(key));
            if (result.contains(name)) {
                throw SaveError("InvalidData", "Checkpoint dictionary keys are not unique after UTF-8 conversion.");
            }
            result[std::move(name)] = convert(dictionary[key], depth + 1);
        }
        return result;
    }
};

} // namespace

std::string utf8(const godot::String &value) {
    const godot::CharString bytes = value.utf8();
    std::string result(bytes.get_data(), static_cast<std::size_t>(bytes.length()));
    check_embedded_nul(result);
    return result;
}

godot::String godot_string(const std::string &value) {
    check_embedded_nul(value);
    if (value.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw SaveError("LimitExceeded", "A checkpoint string exceeds the Godot string limit.");
    }
    godot::String result;
    if (result.parse_utf8(value.data(), static_cast<std::int64_t>(value.size())) != godot::OK) {
        throw SaveError("InvalidData", "A checkpoint string contains invalid UTF-8.");
    }

    // Godot's UTF-8 parser consumes a leading BOM. Here it is part of the string value.
    if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xef &&
            static_cast<unsigned char>(value[1]) == 0xbb && static_cast<unsigned char>(value[2]) == 0xbf) {
        return godot::String::chr(0xfeff) + result;
    }
    return result;
}

Json to_json(const godot::Variant &value, const Limits &limits) {
    return SnapshotConverter(limits).convert(value);
}

godot::Variant to_variant(const Json &value) {
    switch (value.type()) {
        case Json::value_t::null:
            return {};
        case Json::value_t::boolean:
            return value.get<bool>();
        case Json::value_t::number_integer: {
            const auto integer = value.get<std::int64_t>();
            check_exact_integer(integer);
            return integer;
        }
        case Json::value_t::number_unsigned: {
            const auto integer = value.get<std::uint64_t>();
            if (integer > static_cast<std::uint64_t>(max_exact_integer)) {
                throw SaveError("InvalidData", "An integer exceeds the exact JSON range. Use a decimal string.");
            }
            return static_cast<std::int64_t>(integer);
        }
        case Json::value_t::number_float: {
            const auto number = value.get<double>();
            check_finite(number);
            return number;
        }
        case Json::value_t::string:
            return godot_string(value.get_ref<const Json::string_t &>());
        case Json::value_t::array: {
            godot::Array array;
            for (const Json &element : value) {
                array.push_back(to_variant(element));
            }
            return array;
        }
        case Json::value_t::object: {
            godot::Dictionary dictionary;
            for (auto field = value.cbegin(); field != value.cend(); ++field) {
                dictionary[godot_string(field.key())] = to_variant(field.value());
            }
            return dictionary;
        }
        default:
            throw SaveError("InvalidData", "Unsupported value in the JSON data tree.");
    }
}

} // namespace checkpoint
