// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "gameplay_tag_container.hpp"

#include <variant>
#include <vector>

namespace crimson::gameplay_tags {

// Owns a reusable predicate over an explicit gameplay tag set.
class GameplayTagQuery {
public:
    // Combines the results of the operands in one group.
    enum class Match { ANY, ALL, NONE };

    // Creates an unset query, which always fails.
    GameplayTagQuery() = default;
    // Captures tag requirements. Exact groups inspect explicit membership only.
    static GameplayTagQuery tags(Match match, GameplayTagContainer tags, bool exact = false);
    // Captures child queries as one group. An unset child evaluates to false.
    static GameplayTagQuery group(Match match, std::vector<GameplayTagQuery> children);
    // Evaluates this query without changing the subject.
    bool matches(const GameplayTagContainer &subject) const;

private:
    // Holds either the snapshot of tags to test or the nested query operands.
    std::variant<std::monostate, GameplayTagContainer, std::vector<GameplayTagQuery>> operands;
    // Specifies how operands combine.
    Match match_mode = Match::ANY;
    // Restricts tag operands to explicitly held identities.
    bool exact_tags = false;
};

} // namespace crimson::gameplay_tags
