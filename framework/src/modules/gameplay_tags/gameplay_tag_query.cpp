// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag_query.hpp"

#include <algorithm>
#include <utility>

namespace crimson::gameplay_tags {

GameplayTagQuery GameplayTagQuery::tags(Match match, GameplayTagContainer tags, bool exact) {
    GameplayTagQuery query;
    query.match_mode = match;
    query.exact_tags = exact;
    query.operands = std::move(tags);
    return query;
}

GameplayTagQuery GameplayTagQuery::group(Match match, std::vector<GameplayTagQuery> children) {
    GameplayTagQuery query;
    query.match_mode = match;
    query.operands = std::move(children);
    return query;
}

bool GameplayTagQuery::matches(const GameplayTagContainer &subject) const {
    if (const auto *tags = std::get_if<GameplayTagContainer>(&operands)) {
        if (match_mode == Match::ALL) {
            return exact_tags ? subject.has_all_exact(*tags) : subject.has_all(*tags);
        }
        const bool has_any = exact_tags ? subject.has_any_exact(*tags) : subject.has_any(*tags);
        return match_mode == Match::NONE ? !has_any : has_any;
    }
    if (const auto *children = std::get_if<std::vector<GameplayTagQuery>>(&operands)) {
        const auto child_matches = [&subject](const GameplayTagQuery &child) {
            return child.matches(subject);
        };
        if (match_mode == Match::ALL) {
            return std::all_of(children->begin(), children->end(), child_matches);
        }
        const bool has_any = std::any_of(children->begin(), children->end(), child_matches);
        return match_mode == Match::NONE ? !has_any : has_any;
    }
    return false;
}

} // namespace crimson::gameplay_tags
