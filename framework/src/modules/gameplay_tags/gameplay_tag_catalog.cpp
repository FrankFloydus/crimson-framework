// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag_catalog.hpp"

namespace crimson::gameplay_tags {

GameplayTag GameplayTagCatalog::register_tag(std::string_view name) {
    GameplayTag tag = GameplayTag::from_name(name);
    if (!tag.is_valid()) {
        return {};
    }

    const auto existing = entries.find(tag.name());
    if (existing != entries.end()) {
        existing->second = true;
        return tag;
    }

    // Stage the new entries before publishing them so the path is registered together.
    decltype(entries) pending;
    const std::string_view path = tag.name();
    for (std::size_t separator = path.find('.'); separator != std::string_view::npos;
            separator = path.find('.', separator + 1)) {
        const std::string_view ancestor = path.substr(0, separator);
        if (entries.find(ancestor) == entries.end()) {
            pending.emplace(ancestor, false);
        }
    }
    pending.emplace(tag.name(), true);
    entries.merge(pending);
    return tag;
}

GameplayTag GameplayTagCatalog::find(std::string_view name) const {
    GameplayTag tag = GameplayTag::from_name(name);
    return entries.find(tag.name()) == entries.end() ? GameplayTag() : tag;
}

bool GameplayTagCatalog::is_declared(const GameplayTag &tag) const {
    const auto found = entries.find(tag.name());
    return found != entries.end() && found->second;
}

std::vector<GameplayTag> GameplayTagCatalog::tags(bool declared_only) const {
    std::vector<GameplayTag> result;
    for (const auto &[name, declared] : entries) {
        if (declared || !declared_only) {
            result.push_back(GameplayTag(name));
        }
    }
    return result;
}

std::vector<GameplayTag> GameplayTagCatalog::children(const GameplayTag &parent) const {
    return collect_descendants(parent, false);
}

std::vector<GameplayTag> GameplayTagCatalog::descendants(const GameplayTag &parent) const {
    return collect_descendants(parent, true);
}

std::vector<GameplayTag> GameplayTagCatalog::collect_descendants(const GameplayTag &parent, bool recursive) const {
    std::vector<GameplayTag> result;
    if (!parent.is_valid()) {
        return result;
    }

    const std::string prefix = parent.name() + '.';
    for (auto entry = entries.lower_bound(prefix); entry != entries.end(); ++entry) {
        const std::string &candidate = entry->first;
        if (candidate.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        if (recursive || candidate.find('.', prefix.size()) == std::string::npos) {
            result.push_back(GameplayTag(candidate));
        }
    }
    return result;
}

} // namespace crimson::gameplay_tags
