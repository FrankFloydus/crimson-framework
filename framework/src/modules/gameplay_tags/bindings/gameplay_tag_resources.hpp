// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "../gameplay_tag_catalog.hpp"
#include "../gameplay_tag_query.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace crimson::gameplay_tags::bindings {

using TagValue = gameplay_tags::GameplayTag;
using ContainerValue = gameplay_tags::GameplayTagContainer;
using CatalogValue = gameplay_tags::GameplayTagCatalog;
using QueryValue = gameplay_tags::GameplayTagQuery;

class GameplayTagContainer;
class GameplayTagQuery;

// A portable gameplay tag value for Scripts and Inspector properties.
class GameplayTag : public godot::Resource {
    GDCLASS(GameplayTag, godot::Resource)

    // Owns the canonical tag, with an invalid default for new resources.
    TagValue value;

    // Reads the serialized name with project redirects.
    void set_saved_name(const godot::StringName &name);

protected:
    // Exposes tag operations and the canonical saved identity to Godot.
    static void _bind_methods();

public:
    // Wraps a valid native tag, or returns null for an invalid value.
    static godot::Ref<GameplayTag> from_value(TagValue tag);
    // Parses a tag name, including project redirects, or returns null on malformed input.
    static godot::Ref<GameplayTag> from_name(const godot::StringName &name);
    // Looks up a registered tag in the project catalog or returns null.
    static godot::Ref<GameplayTag> find(const godot::StringName &name);
    // Returns the lowercase canonical path, or an empty name for an invalid tag.
    godot::StringName get_tag_name() const;
    // Reports whether the value holds a valid path.
    bool is_valid() const;
    // Reports whether this tag is equal to or descends from the required tag.
    bool matches(const godot::Ref<GameplayTag> &required) const;
    // Reports whether both tags hold the same valid path.
    bool matches_exact(const godot::Ref<GameplayTag> &required) const;
    // Counts shared whole path segments from the root.
    int64_t match_depth(const godot::Ref<GameplayTag> &other) const;
    // Returns the direct parent, or null at the root.
    godot::Ref<GameplayTag> parent() const;
    // Returns the last path segment, or an empty name for an invalid tag.
    godot::StringName leaf_name() const;
    // Returns a container holding this tag as its only explicit member.
    godot::Ref<GameplayTagContainer> as_container() const;
    // Borrows the native value for internal queries.
    const TagValue &get_value() const;
};

// Owns a mutable explicit set that can be stored on a scene or resource.
class GameplayTagContainer : public godot::Resource {
    GDCLASS(GameplayTagContainer, godot::Resource)

    // Stores explicit members and derives inherited matches from them.
    ContainerValue value;

    // Loads and validates all saved names together.
    void set_saved_names(const godot::PackedStringArray &names);
    // Returns explicit canonical paths in insertion order for storage.
    godot::PackedStringArray get_saved_names() const;

protected:
    // Exposes set operations, typed arrays, and persisted tag names.
    static void _bind_methods();

public:
    // Builds a new container with a native value.
    static godot::Ref<GameplayTagContainer> from_value(ContainerValue tags);
    // Creates a container with the supplied valid and unique explicit tags.
    static godot::Ref<GameplayTagContainer> from_tags(const godot::TypedArray<GameplayTag> &tags);
    // Returns a detached copy for independent mutation.
    godot::Ref<GameplayTagContainer> copy() const;
    // Returns explicit tags in insertion order as independent values.
    godot::TypedArray<GameplayTag> get_tags() const;
    // Returns the number of explicit tags.
    int64_t size() const;
    // Reports whether there are zero explicit entries.
    bool is_empty() const;
    // Adds a valid tag and reports whether explicit membership changed.
    bool add(const godot::Ref<GameplayTag> &tag);
    // Adds a valid tag in place of its explicit ancestors, or succeeds if already explicit.
    bool add_leaf(const godot::Ref<GameplayTag> &tag);
    // Removes an exact explicit tag and reports whether it was present.
    bool remove(const godot::Ref<GameplayTag> &tag);
    // Removes all explicit members also explicitly present in the other set.
    void remove_all(const godot::Ref<GameplayTagContainer> &other);
    // Appends missing explicit members from the supplied set.
    void append(const godot::Ref<GameplayTagContainer> &other);
    // Appends explicit source tags that match an explicit requirement.
    void append_matching(const godot::Ref<GameplayTagContainer> &source,
            const godot::Ref<GameplayTagContainer> &required);
    // Removes all explicit members.
    void clear();
    // Checks for an explicit member equal to or descending from the required tag.
    bool has(const godot::Ref<GameplayTag> &tag) const;
    // Checks for exact explicit membership.
    bool has_exact(const godot::Ref<GameplayTag> &tag) const;
    // Checks if at least one explicit requirement is met by name or ancestry.
    bool has_any(const godot::Ref<GameplayTagContainer> &required) const;
    // Checks if every explicit requirement is met by name or ancestry.
    bool has_all(const godot::Ref<GameplayTagContainer> &required) const;
    // Checks if at least one requirement is an exact explicit member.
    bool has_any_exact(const godot::Ref<GameplayTagContainer> &required) const;
    // Checks if every requirement is an exact explicit member.
    bool has_all_exact(const godot::Ref<GameplayTagContainer> &required) const;
    // Copies explicit members that match a requirement by name or ancestry.
    godot::Ref<GameplayTagContainer> filter(const godot::Ref<GameplayTagContainer> &required) const;
    // Copies the exact intersection of the two sets.
    godot::Ref<GameplayTagContainer> filter_exact(const godot::Ref<GameplayTagContainer> &required) const;
    // Returns a copy with each explicit tag and all of its ancestors.
    godot::Ref<GameplayTagContainer> with_parents() const;
    // Compares explicit membership regardless of order.
    bool equals(const godot::Ref<GameplayTagContainer> &other) const;
    // Runs a reusable predicate against this set.
    bool matches_query(const godot::Ref<GameplayTagQuery> &query) const;
    // Borrows the native set for internal queries.
    const ContainerValue &get_value() const;
};

// Owns an independent catalog of declared tags and their implied parents.
class GameplayTagCatalog : public godot::RefCounted {
    GDCLASS(GameplayTagCatalog, godot::RefCounted)

    // Holds a snapshot of registered identities.
    CatalogValue value;
    // Maps old canonical names to current names in a project snapshot.
    godot::Dictionary redirects;

protected:
    // Exposes catalog lookup, traversal, and project authoring methods to Godot.
    static void _bind_methods();

public:
    // Reads the project definitions and redirects into an independent snapshot.
    static godot::Ref<GameplayTagCatalog> project();
    // Adds a tag declaration to Project Settings and saves the project configuration.
    static godot::Error add_project_tag(const godot::StringName &name);
    // Renames a declaration or subtree and records redirects for saved tag references.
    static godot::Error rename_project_tag(const godot::StringName &old_name,
            const godot::StringName &new_name, bool include_children = false);
    // Removes a declared leaf unless a rename redirect would lose its target.
    static godot::Error remove_project_tag(const godot::StringName &name);
    // Registers a name in this instance and returns null for an invalid path.
    godot::Ref<GameplayTag> register_tag(const godot::StringName &name);
    // Looks up a registered canonical name or redirect, with null for a miss.
    godot::Ref<GameplayTag> find(const godot::StringName &name) const;
    // Reports whether a tag was declared explicitly in this catalog.
    bool is_declared(const godot::Ref<GameplayTag> &tag) const;
    // Lists registered names in lexical order with optional declared-only filtering.
    godot::TypedArray<GameplayTag> get_tags(bool declared_only = false) const;
    // Lists immediate children of a registered tag.
    godot::TypedArray<GameplayTag> children(const godot::Ref<GameplayTag> &tag) const;
    // Lists all proper descendants of a registered tag.
    godot::TypedArray<GameplayTag> descendants(const godot::Ref<GameplayTag> &tag) const;
};

// Captures an immutable predicate over tag containers for script calls.
class GameplayTagQuery : public godot::RefCounted {
    GDCLASS(GameplayTagQuery, godot::RefCounted)
    friend class GameplayTagContainer;

    // Owns the full predicate with no dependencies on input objects.
    QueryValue value;

    // Captures explicit requirements for one tag group, with null on missing input.
    static godot::Ref<GameplayTagQuery> make_tags(QueryValue::Match mode,
            const godot::Ref<GameplayTagContainer> &terms, bool exact);
    // Captures child predicates for one query group, rejecting null child entries.
    static godot::Ref<GameplayTagQuery> make_group(QueryValue::Match mode,
            const godot::TypedArray<GameplayTagQuery> &queries);

protected:
    // Exposes the query factories and evaluator to Godot.
    static void _bind_methods();

public:
    // Matches at least one listed tag.
    static godot::Ref<GameplayTagQuery> any_tags(const godot::Ref<GameplayTagContainer> &terms, bool exact = false);
    // Matches every listed tag.
    static godot::Ref<GameplayTagQuery> all_tags(const godot::Ref<GameplayTagContainer> &terms, bool exact = false);
    // Matches only when all listed tags are absent.
    static godot::Ref<GameplayTagQuery> no_tags(const godot::Ref<GameplayTagContainer> &terms, bool exact = false);
    // Matches at least one child query.
    static godot::Ref<GameplayTagQuery> any_queries(const godot::TypedArray<GameplayTagQuery> &queries);
    // Matches all child queries.
    static godot::Ref<GameplayTagQuery> all_queries(const godot::TypedArray<GameplayTagQuery> &queries);
    // Matches when no child query matches.
    static godot::Ref<GameplayTagQuery> no_queries(const godot::TypedArray<GameplayTagQuery> &queries);
    // Evaluates the predicate on a container. A null container fails.
    bool matches(const godot::Ref<GameplayTagContainer> &tags) const;
};

// Registers the tag types and project setting metadata during scene initialization.
void register_gameplay_tag_bindings(godot::ModuleInitializationLevel level);

} // namespace crimson::gameplay_tags::bindings
