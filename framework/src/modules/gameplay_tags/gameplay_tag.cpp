// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag.hpp"

#include <utility>

namespace crimson::gameplay_tags {

GameplayTag::GameplayTag(std::string name) noexcept : canonical_name(std::move(name)) {}

GameplayTag GameplayTag::from_name(std::string_view name) {
    std::string canonical;
    canonical.reserve(name.size());
    bool needs_segment = true;
    for (char character : name) {
        if (character == '.') {
            if (needs_segment) {
                return {};
            }
            canonical.push_back(character);
            needs_segment = true;
            continue;
        }
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
        const bool letter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !digit && character != '_') {
            return {};
        }
        canonical.push_back(character);
        needs_segment = false;
    }
    return needs_segment ? GameplayTag() : GameplayTag(std::move(canonical));
}

GameplayTag::GameplayTag(GameplayTag &&other) noexcept : canonical_name(std::move(other.canonical_name)) {
    other.canonical_name.clear();
}

GameplayTag &GameplayTag::operator=(GameplayTag &&other) noexcept {
    if (this != &other) {
        canonical_name = std::move(other.canonical_name);
        other.canonical_name.clear();
    }
    return *this;
}

bool GameplayTag::is_valid() const noexcept { return !canonical_name.empty(); }

const std::string &GameplayTag::name() const noexcept { return canonical_name; }

bool GameplayTag::matches(const GameplayTag &required) const noexcept {
    const std::size_t required_size = required.canonical_name.size();
    if (!required.is_valid() || canonical_name.size() < required_size) {
        return false;
    }
    return canonical_name.compare(0, required_size, required.canonical_name) == 0 &&
            (canonical_name.size() == required_size || canonical_name[required_size] == '.');
}

bool GameplayTag::matches_exact(const GameplayTag &required) const noexcept {
    return is_valid() && *this == required;
}

std::size_t GameplayTag::match_depth(const GameplayTag &other) const noexcept {
    std::string_view left = canonical_name;
    std::string_view right = other.canonical_name;
    std::size_t depth = 0;
    while (!left.empty() && !right.empty()) {
        const std::size_t left_end = left.find('.');
        const std::size_t right_end = right.find('.');
        if (left.substr(0, left_end) != right.substr(0, right_end)) {
            break;
        }
        ++depth;
        if (left_end == std::string_view::npos || right_end == std::string_view::npos) {
            break;
        }
        left.remove_prefix(left_end + 1);
        right.remove_prefix(right_end + 1);
    }
    return depth;
}

GameplayTag GameplayTag::parent() const {
    const std::size_t separator = canonical_name.rfind('.');
    if (separator == std::string::npos) {
        return {};
    }
    return GameplayTag(canonical_name.substr(0, separator));
}

std::string_view GameplayTag::leaf_name() const noexcept {
    const std::size_t separator = canonical_name.rfind('.');
    const std::string_view canonical = canonical_name;
    if (separator == std::string::npos) {
        return canonical;
    }
    return canonical.substr(separator + 1);
}

bool GameplayTag::operator==(const GameplayTag &other) const noexcept {
    return canonical_name == other.canonical_name;
}

bool GameplayTag::operator!=(const GameplayTag &other) const noexcept { return !(*this == other); }

} // namespace crimson::gameplay_tags
