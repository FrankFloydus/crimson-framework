// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace crimson::gameplay_tags {

class GameplayTagCatalog;

// A validated hierarchical name that can outlive the catalog that resolved it.
class GameplayTag {
    friend class GameplayTagCatalog;

    // The canonical lowercase name, or an empty string for an invalid tag.
    std::string canonical_name;

    // Constructs a value from a canonical name already validated by the catalog.
    explicit GameplayTag(std::string name) noexcept;

public:
    // Parses and normalizes a path regardless of catalog membership. Returns an invalid tag on malformed input.
    static GameplayTag from_name(std::string_view name);

    // Creates an invalid tag.
    GameplayTag() = default;
    // Copies the canonical name independently of the source.
    GameplayTag(const GameplayTag &) = default;
    // Moves the name and leaves the source invalid.
    GameplayTag(GameplayTag &&other) noexcept;
    // Replaces the canonical name with an independent copy.
    GameplayTag &operator=(const GameplayTag &) = default;
    // Moves the name and leaves a distinct source invalid.
    GameplayTag &operator=(GameplayTag &&other) noexcept;

    // Reports whether the value holds a valid name.
    bool is_valid() const noexcept;
    // Borrows the canonical name until this tag is changed or destroyed. Invalid values have an empty name.
    const std::string &name() const noexcept;
    // Checks whether this tag is equal to or a descendant of the required tag.
    bool matches(const GameplayTag &required) const noexcept;
    // Checks whether both values hold the same valid tag.
    bool matches_exact(const GameplayTag &required) const noexcept;
    // Counts matching path segments starting at the root.
    std::size_t match_depth(const GameplayTag &other) const noexcept;
    // Returns the direct parent, or an invalid tag at the root.
    GameplayTag parent() const;
    // Views the last segment until this tag is changed or destroyed.
    std::string_view leaf_name() const noexcept;

    // Compares canonical identity, treating two invalid values as equal.
    bool operator==(const GameplayTag &other) const noexcept;
    // Reports whether the two values have different identities.
    bool operator!=(const GameplayTag &other) const noexcept;
};

} // namespace crimson::gameplay_tags
