// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "gameplay_tag.hpp"

#include <functional>
#include <map>
#include <vector>

namespace crimson::gameplay_tags {

// Owns the tag vocabulary for a game or tool, including all implied ancestors.
class GameplayTagCatalog {
    // Canonical names in lexical order. A true entry was registered explicitly.
    std::map<std::string, bool, std::less<>> entries;

    // Finds the ordered children or descendants under a valid parent name.
    std::vector<GameplayTag> collect_descendants(const GameplayTag &parent, bool recursive) const;

public:
    // Declares a name and registers its ancestors. Duplicate declarations are idempotent.
    // Accepts dot-separated ASCII letters, digits, and underscores. Invalid names return an invalid tag.
    GameplayTag register_tag(std::string_view name);
    // Resolves an explicit or implicit name, ignoring ASCII case. Unknown or invalid names return an invalid tag.
    GameplayTag find(std::string_view name) const;
    // Reports whether this catalog contains an explicit declaration for the tag.
    bool is_declared(const GameplayTag &tag) const;
    // Lists registered tags in lexical order, optionally skipping implicit ancestors.
    std::vector<GameplayTag> tags(bool declared_only = false) const;
    // Lists direct children in lexical order. Returns an empty list for an invalid or unknown parent.
    std::vector<GameplayTag> children(const GameplayTag &parent) const;
    // Lists all descendants in lexical order. Returns an empty list for an invalid or unknown parent.
    std::vector<GameplayTag> descendants(const GameplayTag &parent) const;
};

} // namespace crimson::gameplay_tags
