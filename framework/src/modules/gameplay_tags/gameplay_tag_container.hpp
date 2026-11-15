// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "gameplay_tag.hpp"

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace crimson::gameplay_tags {

// Owns distinct explicit tags in insertion order and resolves parent membership on demand.
class GameplayTagContainer {
    // Contains only valid tags explicitly added to this container, with no duplicates.
    std::vector<GameplayTag> explicit_tags;

public:
    // Creates an empty tag set.
    GameplayTagContainer() = default;
    // Adds valid, distinct initial tags in the supplied order.
    GameplayTagContainer(std::initializer_list<GameplayTag> initial_tags);

    // Adds a valid explicit tag and returns true if the set changed.
    bool add(const GameplayTag &tag);
    // Keeps an existing exact tag or inserts a tag in place of its explicit ancestors.
    // Returns false for an invalid tag or a tag already represented by a descendant.
    bool add_leaf(const GameplayTag &tag);
    // Removes an exact explicit tag and returns true if the set changed.
    bool remove(const GameplayTag &tag);
    // Removes every explicit tag also explicitly present in the supplied set.
    void remove_all(const GameplayTagContainer &other);
    // Adds missing explicit tags from the supplied set in its insertion order.
    void append(const GameplayTagContainer &other);
    // Adds explicit source tags that match any explicit requirement by name or ancestry.
    void append_matching(const GameplayTagContainer &source, const GameplayTagContainer &required);
    // Removes all explicit tags.
    void clear();

    // Reports whether an explicit tag equals or descends from the requested tag.
    bool has(const GameplayTag &tag) const;
    // Reports whether the requested valid tag is explicitly present.
    bool has_exact(const GameplayTag &tag) const;
    // Reports whether any explicit requirement is present by name or ancestry. Empty requirements fail.
    bool has_any(const GameplayTagContainer &required) const;
    // Reports whether every explicit requirement is present by name or ancestry. Empty requirements pass.
    bool has_all(const GameplayTagContainer &required) const;
    // Reports whether any explicit requirement is also explicit here. Empty requirements fail.
    bool has_any_exact(const GameplayTagContainer &required) const;
    // Reports whether every explicit requirement is also explicit here. Empty requirements pass.
    bool has_all_exact(const GameplayTagContainer &required) const;

    // Returns original explicit tags that equal or descend from any explicit requirement.
    GameplayTagContainer filter(const GameplayTagContainer &required) const;
    // Returns the exact intersection in this container's insertion order.
    GameplayTagContainer filter_exact(const GameplayTagContainer &required) const;
    // Copies the explicit tags and adds their ancestors as explicit tags.
    GameplayTagContainer with_parents() const;

    // Returns the number of explicit tags.
    std::size_t size() const;
    // Reports whether the container contains zero explicit tags.
    bool empty() const;
    // Borrows the explicit tag sequence in insertion order. Mutations may invalidate element references.
    const std::vector<GameplayTag> &tags() const;

    // Compares explicit membership regardless of insertion order.
    bool operator==(const GameplayTagContainer &other) const;
    // Reports whether the sets differ in explicit membership.
    bool operator!=(const GameplayTagContainer &other) const;
};

} // namespace crimson::gameplay_tags
