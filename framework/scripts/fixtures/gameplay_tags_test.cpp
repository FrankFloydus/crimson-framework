// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/gameplay_tags/gameplay_tag_catalog.hpp"
#include "modules/gameplay_tags/gameplay_tag_container.hpp"
#include "modules/gameplay_tags/gameplay_tag_query.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using crimson::gameplay_tags::GameplayTag;
using crimson::gameplay_tags::GameplayTagCatalog;
using crimson::gameplay_tags::GameplayTagContainer;
using crimson::gameplay_tags::GameplayTagQuery;

/** Reports a failed contract check in both debug and release builds. */
void check(bool condition, std::string_view description) {
    if (!condition) {
        std::cerr << "Gameplay Tags: " << description << '\n';
        std::exit(EXIT_FAILURE);
    }
}

/** Checks registration, path validation, traversal, and detached value lifetime. */
void test_catalog_and_values() {
    GameplayTagCatalog catalog;
    const GameplayTag stun = catalog.register_tag("State.Control.Stun");
    const GameplayTag slow = catalog.register_tag("state.control.slow");
    const GameplayTag control = catalog.find("STATE.Control");
    const GameplayTag state = catalog.find("state");
    check(stun.is_valid() && stun.name() == "state.control.stun", "registration canonicalizes ASCII case");
    check(catalog.find("STATE.Control.STUN") == stun, "lookup ignores ASCII case");
    check(catalog.register_tag("STATE.CONTROL.STUN") == stun, "equivalent declarations are idempotent");
    check(catalog.is_declared(stun) && !catalog.is_declared(control), "implicit ancestors are registered");
    check(catalog.tags(true) == std::vector<GameplayTag>{slow, stun}, "declared enumeration is sorted");
    check(catalog.tags() == std::vector<GameplayTag>{state, control, slow, stun}, "enumeration includes ancestors");
    check(catalog.register_tag("State.Control") == control && catalog.is_declared(control), "implicit parent can be declared");
    check(catalog.children(state) == std::vector<GameplayTag>{control}, "children returns immediate descendants");
    check(catalog.descendants(state) == std::vector<GameplayTag>{control, slow, stun}, "descendants excludes its root");
    check(catalog.children(stun).empty() && catalog.descendants(GameplayTag{}).empty(), "leaf and invalid traversal is empty");

    const std::vector<std::string_view> invalid_names{
        "", ".", ".state", "state.", "state..stun", " state", "state stun", "state/stun",
        "state-stun", "state.\tstun", "state.\xC3\xA9", std::string_view("state\0stun", 10),
    };
    const auto registered = catalog.tags();
    for (const auto invalid_name : invalid_names) {
        check(!catalog.register_tag(invalid_name).is_valid(), "registration rejects malformed path");
        check(!catalog.find(invalid_name).is_valid(), "lookup rejects malformed path");
    }
    check(catalog.tags() == registered, "a malformed declaration adds no partial ancestors");
    check(!catalog.find("state.missing").is_valid(), "lookup does not create unknown names");
    check(catalog.register_tag("Class_2.3").name() == "class_2.3", "segments accept digits and underscores");

    check(stun.matches(stun) && stun.matches(control) && stun.matches(state), "tag matches itself and ancestors");
    check(!state.matches(stun) && !stun.matches(slow), "tag matching is directional and excludes siblings");
    const GameplayTag different_root = catalog.register_tag("states.control.stun");
    check(!different_root.matches(state), "string prefix alone cannot match a segment");
    check(stun.matches_exact(stun) && !stun.matches_exact(control), "exact matching compares full identity");
    check(stun.parent() == control && control.parent() == state && !state.parent().is_valid(), "direct parent follows the path");
    check(stun.leaf_name() == "stun" && state.leaf_name() == "state", "leaf is the final segment");
    check(stun.match_depth(slow) == 2 && stun.match_depth(stun) == 3 && stun.match_depth(different_root) == 0, "depth counts shared leading segments");

    const GameplayTag invalid;
    check(invalid == GameplayTag{} && !invalid.matches_exact(invalid), "invalid equality differs from exact gameplay matching");
    check(!invalid.matches(stun) && !stun.matches(invalid) && !invalid.matches_exact(stun), "invalid operands fail matching");
    check(!invalid.parent().is_valid() && invalid.leaf_name().empty() && invalid.match_depth(stun) == 0, "invalid introspection is empty");

    GameplayTag detached;
    {
        GameplayTagCatalog other;
        check(!other.find(stun.name()).is_valid(), "catalogs have independent registrations");
        check(other.children(state).empty(), "walking a catalog requires a registered root");
        detached = other.register_tag("state.control.STUN");
        check(detached == stun, "canonical identity is stable across catalogs");
    }
    check(detached.parent() == control && detached.matches(state), "tag values survive their catalog");
}

/** Checks hierarchical membership, explicit membership, and owned value copies. */
void test_membership() {
    GameplayTagCatalog catalog;
    const GameplayTag stun = catalog.register_tag("state.control.stun");
    const GameplayTag slow = catalog.register_tag("state.control.slow");
    const GameplayTag airborne = catalog.register_tag("movement.airborne");
    const GameplayTag control = stun.parent();
    const GameplayTag state = control.parent();
    const GameplayTag invalid;
    const GameplayTagContainer no_tags;

    GameplayTagContainer tags{invalid, stun, stun, airborne};
    check(tags.size() == 2 && !tags.empty(), "bulk construction keeps unique valid members");
    check(tags.tags() == std::vector<GameplayTag>{stun, airborne}, "enumeration returns explicit tags in insertion order");
    check(tags.has(control) && tags.has(state) && !tags.has_exact(control), "only hierarchical membership sees implied ancestors");
    check(!tags.has(slow) && !tags.has(invalid) && !tags.has_exact(invalid), "siblings and invalid values are absent");
    check(tags.has_any({control, slow}) && !tags.has_any_exact({control, slow}), "any checks explicit operands");
    check(tags.has_all({state, airborne}) && !tags.has_all_exact({state, airborne}), "all checks explicit operands");
    check(!tags.has_all({control, slow}) && tags.has_all_exact({stun, airborne}), "one missing requirement fails all");
    check(!tags.has_any(no_tags) && !tags.has_any_exact(no_tags) && tags.has_all(no_tags) && tags.has_all_exact(no_tags), "empty requirements obey any and all identities");
    check(!no_tags.has_any(tags) && !no_tags.has_all(tags) && no_tags.has_all(no_tags), "empty source can satisfy only empty requirements");
    check(GameplayTagContainer{state}.has_all({state}) && !GameplayTagContainer{state}.has_all({stun}), "parent membership does not imply child membership");

    const GameplayTagContainer expanded = tags.with_parents();
    check(expanded == GameplayTagContainer{stun, control, state, airborne, airborne.parent()}, "parent expansion materializes ancestors");
    check(expanded.has_exact(control) && !tags.has_exact(control), "expansion owns an independent copy");
    check(tags == GameplayTagContainer{airborne, stun} && tags != expanded, "equality compares explicit sets regardless of insertion order");
    GameplayTagContainer copy = tags;
    check(copy.add(control) && copy.has_exact(control) && !tags.has_exact(control), "normal insertion retains explicit parents and children in the copy");
    check(!copy.add(stun) && !copy.add(invalid), "normal insertion rejects duplicates and invalid values");
    check(copy.remove(control) && copy.has(control) && !copy.has_exact(control), "removing an explicit ancestor preserves implied matching");
    check(!copy.remove(control), "removing an implied ancestor has no effect");
    copy.clear();
    check(copy.empty() && tags.size() == 2, "clearing a copy leaves its source intact");
}

/** Checks leaf insertion, set operations, and mutation with aliased operands. */
void test_mutation_and_filters() {
    GameplayTagCatalog catalog;
    const GameplayTag stun = catalog.register_tag("state.control.stun");
    const GameplayTag slow = catalog.register_tag("state.control.slow");
    const GameplayTag control = stun.parent();
    const GameplayTag state = control.parent();
    const GameplayTag airborne = catalog.register_tag("movement.airborne");

    GameplayTagContainer leaves{state, control, airborne};
    check(leaves.add_leaf(stun) && leaves == GameplayTagContainer{airborne, stun}, "leaf insertion removes every explicit ancestor");
    check(leaves.add_leaf(leaves.tags().back()) && leaves.size() == 2, "an existing exact member succeeds without duplication");
    check(!leaves.add_leaf(control) && !leaves.add_leaf(GameplayTag{}), "an implied ancestor or invalid input cannot be inserted as a leaf");
    check(leaves.add_leaf(slow) && leaves.has_exact(slow), "leaf insertion preserves siblings");
    check(leaves.remove(leaves.tags()[1]) && leaves.has(control) && !leaves.has_exact(stun), "exact removal accepts an aliased member and retains shared parents");
    leaves.remove_all({state, slow});
    check(!leaves.has(state) && leaves == GameplayTagContainer{airborne}, "bulk removal removes explicit identities only");

    const GameplayTagContainer source{stun, slow, airborne};
    const GameplayTagContainer required{control, airborne};
    check(source.filter(required) == source, "hierarchical filter retains explicit source members");
    check(source.filter_exact(required) == GameplayTagContainer{airborne}, "exact filter intersects explicit members");
    check(required.filter({stun}).empty() && source.filter({stun}) == GameplayTagContainer{stun}, "filter is directional and excludes siblings");

    GameplayTagContainer combined{control};
    combined.append_matching(source, {stun});
    check(combined == GameplayTagContainer{control, stun}, "matching append retains only qualifying source members");
    combined.append_matching(combined, {state});
    combined.append_matching(source, combined);
    check(combined == GameplayTagContainer{control, stun, slow}, "matching append accepts either operand alias");
    combined.append(combined);
    combined.append(source);
    check(combined == GameplayTagContainer{control, stun, slow, airborne}, "append deduplicates including self append");
    combined.remove_all(combined);
    check(combined.empty(), "removing self clears the container");
}

/** Checks nested tag conditions, exact mode, and independent requirement snapshots. */
void test_queries() {
    using Match = GameplayTagQuery::Match;
    const GameplayTag stun = GameplayTag::from_name("state.control.stun");
    const GameplayTag control = stun.parent();
    GameplayTagContainer requirements{control};
    const GameplayTagQuery controlled = GameplayTagQuery::tags(Match::ALL, requirements);
    const GameplayTagQuery explicit_control = GameplayTagQuery::tags(Match::ANY, requirements, true);
    const GameplayTagQuery inherited_only = GameplayTagQuery::group(Match::ALL, {
        controlled, GameplayTagQuery::group(Match::NONE, {explicit_control}),
    });
    requirements.clear();
    check(inherited_only.matches({stun}), "nested query owns its original requirements");
    check(!inherited_only.matches({stun, control}), "exact nested query sees an explicit parent");
    check(!GameplayTagQuery().matches({stun}), "an unset query fails");
    check(!GameplayTagQuery::tags(Match::ANY, {}).matches({stun}), "an empty tag any fails");
    check(GameplayTagQuery::tags(Match::ALL, {}).matches({stun}), "an empty tag all passes");
    check(GameplayTagQuery::group(Match::NONE, {}).matches({stun}), "an empty group none passes");
}

} // namespace

/** Runs the standalone native Gameplay Tags contracts. */
int main() {
    test_catalog_and_values();
    test_membership();
    test_mutation_and_filters();
    test_queries();
    std::cout << "Gameplay Tags tests passed\n";
}
