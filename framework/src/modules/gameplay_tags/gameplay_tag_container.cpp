// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag_container.hpp"

#include <algorithm>

namespace crimson::gameplay_tags {

GameplayTagContainer::GameplayTagContainer(std::initializer_list<GameplayTag> initial_tags) {
    explicit_tags.reserve(initial_tags.size());
    for (const GameplayTag &tag : initial_tags) {
        add(tag);
    }
}

bool GameplayTagContainer::add(const GameplayTag &tag) {
    if (!tag.is_valid() || has_exact(tag)) {
        return false;
    }
    explicit_tags.push_back(tag);
    return true;
}

bool GameplayTagContainer::add_leaf(const GameplayTag &tag) {
    if (!tag.is_valid()) {
        return false;
    }
    if (has_exact(tag)) {
        return true;
    }
    if (has(tag)) {
        return false;
    }

    explicit_tags.push_back(tag);
    const auto new_leaf = explicit_tags.end() - 1;
    const auto kept_end = std::remove_if(explicit_tags.begin(), new_leaf, [&tag](const GameplayTag &candidate) {
        return tag.matches(candidate);
    });
    explicit_tags.erase(kept_end, new_leaf);
    return true;
}

bool GameplayTagContainer::remove(const GameplayTag &tag) {
    const auto found = std::find_if(explicit_tags.begin(), explicit_tags.end(), [&tag](const GameplayTag &candidate) {
        return candidate.matches_exact(tag);
    });
    if (found == explicit_tags.end()) {
        return false;
    }
    explicit_tags.erase(found);
    return true;
}

void GameplayTagContainer::remove_all(const GameplayTagContainer &other) {
    if (this == &other) {
        clear();
        return;
    }
    const auto removed_begin = std::remove_if(explicit_tags.begin(), explicit_tags.end(), [&other](const GameplayTag &candidate) {
        return other.has_exact(candidate);
    });
    explicit_tags.erase(removed_begin, explicit_tags.end());
}

void GameplayTagContainer::append(const GameplayTagContainer &other) {
    if (this == &other) {
        return;
    }
    for (const GameplayTag &tag : other.explicit_tags) {
        add(tag);
    }
}

void GameplayTagContainer::append_matching(const GameplayTagContainer &source, const GameplayTagContainer &required) {
    append(source.filter(required));
}

void GameplayTagContainer::clear() {
    explicit_tags.clear();
}

bool GameplayTagContainer::has(const GameplayTag &tag) const {
    return std::any_of(explicit_tags.begin(), explicit_tags.end(), [&tag](const GameplayTag &candidate) {
        return candidate.matches(tag);
    });
}

bool GameplayTagContainer::has_exact(const GameplayTag &tag) const {
    return std::any_of(explicit_tags.begin(), explicit_tags.end(), [&tag](const GameplayTag &candidate) {
        return candidate.matches_exact(tag);
    });
}

bool GameplayTagContainer::has_any(const GameplayTagContainer &required) const {
    return std::any_of(required.explicit_tags.begin(), required.explicit_tags.end(), [this](const GameplayTag &tag) {
        return has(tag);
    });
}

bool GameplayTagContainer::has_all(const GameplayTagContainer &required) const {
    return std::all_of(required.explicit_tags.begin(), required.explicit_tags.end(), [this](const GameplayTag &tag) {
        return has(tag);
    });
}

bool GameplayTagContainer::has_any_exact(const GameplayTagContainer &required) const {
    return std::any_of(required.explicit_tags.begin(), required.explicit_tags.end(), [this](const GameplayTag &tag) {
        return has_exact(tag);
    });
}

bool GameplayTagContainer::has_all_exact(const GameplayTagContainer &required) const {
    return std::all_of(required.explicit_tags.begin(), required.explicit_tags.end(), [this](const GameplayTag &tag) {
        return has_exact(tag);
    });
}

GameplayTagContainer GameplayTagContainer::filter(const GameplayTagContainer &required) const {
    GameplayTagContainer result;
    for (const GameplayTag &candidate : explicit_tags) {
        const bool matches_requirement = std::any_of(required.explicit_tags.begin(), required.explicit_tags.end(),
                [&candidate](const GameplayTag &tag) { return candidate.matches(tag); });
        if (matches_requirement) {
            result.explicit_tags.push_back(candidate);
        }
    }
    return result;
}

GameplayTagContainer GameplayTagContainer::filter_exact(const GameplayTagContainer &required) const {
    GameplayTagContainer result;
    for (const GameplayTag &candidate : explicit_tags) {
        if (required.has_exact(candidate)) {
            result.explicit_tags.push_back(candidate);
        }
    }
    return result;
}

GameplayTagContainer GameplayTagContainer::with_parents() const {
    GameplayTagContainer result = *this;
    for (const GameplayTag &tag : explicit_tags) {
        for (GameplayTag parent = tag.parent(); parent.is_valid(); parent = parent.parent()) {
            result.add(parent);
        }
    }
    return result;
}

std::size_t GameplayTagContainer::size() const {
    return explicit_tags.size();
}

bool GameplayTagContainer::empty() const {
    return explicit_tags.empty();
}

const std::vector<GameplayTag> &GameplayTagContainer::tags() const {
    return explicit_tags;
}

bool GameplayTagContainer::operator==(const GameplayTagContainer &other) const {
    return size() == other.size() && has_all_exact(other);
}

bool GameplayTagContainer::operator!=(const GameplayTagContainer &other) const {
    return !(*this == other);
}

} // namespace crimson::gameplay_tags
