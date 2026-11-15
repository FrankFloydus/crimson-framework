// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag_names.hpp"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <utility>

namespace crimson::gameplay_tags::bindings {

using namespace godot;

TagValue parse_tag_name(const StringName &name) {
    const CharString utf8_name = String(name).utf8();
    return TagValue::from_name(std::string_view(utf8_name.get_data(), static_cast<size_t>(utf8_name.length())));
}

StringName canonical_tag_name(const TagValue &tag) { return StringName(tag.name().c_str()); }

TagValue resolve_tag_name(TagValue tag, const Dictionary &redirects) {
    if (!tag.is_valid() || redirects.is_empty()) {
        return tag;
    }
    const StringName key = canonical_tag_name(tag);
    if (redirects.has(key)) {
        const TagValue target = parse_tag_name(redirects[key]);
        if (target.is_valid()) {
            return target;
        }
    }
    return tag;
}

TagValue read_tag_name(const StringName &name) {
    TagValue tag = parse_tag_name(name);
    if (!tag.is_valid()) {
        return tag;
    }
    const auto *settings = ProjectSettings::get_singleton();
    const Dictionary redirects = settings->get_setting(redirects_setting, Dictionary());
    if (redirects.has(canonical_tag_name(tag))) {
        const Ref<GameplayTag> resolved = GameplayTagCatalog::project()->find(name);
        if (resolved.is_valid()) {
            return resolved->get_value();
        }
    }
    return tag;
}

TypedArray<GameplayTag> wrap_tags(const std::vector<TagValue> &tags) {
    TypedArray<GameplayTag> result;
    for (const TagValue &tag : tags) {
        result.append(GameplayTag::from_value(tag));
    }
    return result;
}

void GameplayTag::_bind_methods() {
    ClassDB::bind_static_method("GameplayTag", D_METHOD("from_name", "name"), &GameplayTag::from_name);
    ClassDB::bind_static_method("GameplayTag", D_METHOD("find", "name"), &GameplayTag::find);
    ClassDB::bind_method(D_METHOD("get_tag_name"), &GameplayTag::get_tag_name);
    ClassDB::bind_method(D_METHOD("is_valid"), &GameplayTag::is_valid);
    ClassDB::bind_method(D_METHOD("matches", "required"), &GameplayTag::matches);
    ClassDB::bind_method(D_METHOD("matches_exact", "required"), &GameplayTag::matches_exact);
    ClassDB::bind_method(D_METHOD("match_depth", "other"), &GameplayTag::match_depth);
    ClassDB::bind_method(D_METHOD("parent"), &GameplayTag::parent);
    ClassDB::bind_method(D_METHOD("leaf_name"), &GameplayTag::leaf_name);
    ClassDB::bind_method(D_METHOD("as_container"), &GameplayTag::as_container);
    ClassDB::bind_method(D_METHOD("_set_saved_name", "name"), &GameplayTag::set_saved_name);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "_tag_name", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "_set_saved_name", "get_tag_name");
}

Ref<GameplayTag> GameplayTag::from_value(TagValue tag) {
    Ref<GameplayTag> result;
    if (tag.is_valid()) {
        result.instantiate();
        result->value = std::move(tag);
    }
    return result;
}

Ref<GameplayTag> GameplayTag::from_name(const StringName &name) { return from_value(read_tag_name(name)); }

Ref<GameplayTag> GameplayTag::find(const StringName &name) { return GameplayTagCatalog::project()->find(name); }

StringName GameplayTag::get_tag_name() const { return canonical_tag_name(value); }

bool GameplayTag::is_valid() const { return value.is_valid(); }

bool GameplayTag::matches(const Ref<GameplayTag> &required) const {
    return required.is_valid() && value.matches(required->value);
}

bool GameplayTag::matches_exact(const Ref<GameplayTag> &required) const {
    return required.is_valid() && value.matches_exact(required->value);
}

int64_t GameplayTag::match_depth(const Ref<GameplayTag> &other) const {
    return other.is_valid() ? static_cast<int64_t>(value.match_depth(other->value)) : 0;
}

Ref<GameplayTag> GameplayTag::parent() const { return from_value(value.parent()); }

StringName GameplayTag::leaf_name() const { return StringName(std::string(value.leaf_name()).c_str()); }

Ref<GameplayTagContainer> GameplayTag::as_container() const {
    return GameplayTagContainer::from_value({value});
}

const TagValue &GameplayTag::get_value() const { return value; }

void GameplayTag::set_saved_name(const StringName &name) {
    TagValue next = read_tag_name(name);
    if (!name.is_empty() && !next.is_valid()) {
        UtilityFunctions::push_error("[Crimson] Invalid saved Gameplay Tag. Use a dot-separated tag path.");
        return;
    }
    if (value != next) {
        value = std::move(next);
        emit_changed();
    }
}

void GameplayTagContainer::_bind_methods() {
    ClassDB::bind_static_method("GameplayTagContainer", D_METHOD("from_tags", "tags"), &GameplayTagContainer::from_tags);
    ClassDB::bind_method(D_METHOD("copy"), &GameplayTagContainer::copy);
    ClassDB::bind_method(D_METHOD("get_tags"), &GameplayTagContainer::get_tags);
    ClassDB::bind_method(D_METHOD("size"), &GameplayTagContainer::size);
    ClassDB::bind_method(D_METHOD("is_empty"), &GameplayTagContainer::is_empty);
    ClassDB::bind_method(D_METHOD("add", "tag"), &GameplayTagContainer::add);
    ClassDB::bind_method(D_METHOD("add_leaf", "tag"), &GameplayTagContainer::add_leaf);
    ClassDB::bind_method(D_METHOD("remove", "tag"), &GameplayTagContainer::remove);
    ClassDB::bind_method(D_METHOD("remove_all", "other"), &GameplayTagContainer::remove_all);
    ClassDB::bind_method(D_METHOD("append", "other"), &GameplayTagContainer::append);
    ClassDB::bind_method(D_METHOD("append_matching", "source", "required"), &GameplayTagContainer::append_matching);
    ClassDB::bind_method(D_METHOD("clear"), &GameplayTagContainer::clear);
    ClassDB::bind_method(D_METHOD("has", "tag"), &GameplayTagContainer::has);
    ClassDB::bind_method(D_METHOD("has_exact", "tag"), &GameplayTagContainer::has_exact);
    ClassDB::bind_method(D_METHOD("has_any", "required"), &GameplayTagContainer::has_any);
    ClassDB::bind_method(D_METHOD("has_all", "required"), &GameplayTagContainer::has_all);
    ClassDB::bind_method(D_METHOD("has_any_exact", "required"), &GameplayTagContainer::has_any_exact);
    ClassDB::bind_method(D_METHOD("has_all_exact", "required"), &GameplayTagContainer::has_all_exact);
    ClassDB::bind_method(D_METHOD("filter", "required"), &GameplayTagContainer::filter);
    ClassDB::bind_method(D_METHOD("filter_exact", "required"), &GameplayTagContainer::filter_exact);
    ClassDB::bind_method(D_METHOD("with_parents"), &GameplayTagContainer::with_parents);
    ClassDB::bind_method(D_METHOD("equals", "other"), &GameplayTagContainer::equals);
    ClassDB::bind_method(D_METHOD("matches_query", "query"), &GameplayTagContainer::matches_query);
    ClassDB::bind_method(D_METHOD("_set_saved_names", "names"), &GameplayTagContainer::set_saved_names);
    ClassDB::bind_method(D_METHOD("_get_saved_names"), &GameplayTagContainer::get_saved_names);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "_tag_names", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "_set_saved_names", "_get_saved_names");
}

Ref<GameplayTagContainer> GameplayTagContainer::from_value(ContainerValue tags) {
    Ref<GameplayTagContainer> result;
    result.instantiate();
    result->value = std::move(tags);
    return result;
}

Ref<GameplayTagContainer> GameplayTagContainer::from_tags(const TypedArray<GameplayTag> &tags) {
    ContainerValue result;
    for (int64_t index = 0; index < tags.size(); ++index) {
        const Ref<GameplayTag> tag = tags[index];
        if (tag.is_valid()) {
            result.add(tag->get_value());
        }
    }
    return from_value(std::move(result));
}

Ref<GameplayTagContainer> GameplayTagContainer::copy() const { return from_value(value); }

TypedArray<GameplayTag> GameplayTagContainer::get_tags() const { return wrap_tags(value.tags()); }

int64_t GameplayTagContainer::size() const { return static_cast<int64_t>(value.size()); }

bool GameplayTagContainer::is_empty() const { return value.empty(); }

bool GameplayTagContainer::add(const Ref<GameplayTag> &tag) {
    if (tag.is_null() || !value.add(tag->get_value())) {
        return false;
    }
    emit_changed();
    return true;
}

bool GameplayTagContainer::add_leaf(const Ref<GameplayTag> &tag) {
    if (tag.is_null()) {
        return false;
    }
    if (value.has_exact(tag->get_value())) {
        return true;
    }
    if (!value.add_leaf(tag->get_value())) {
        return false;
    }
    emit_changed();
    return true;
}

bool GameplayTagContainer::remove(const Ref<GameplayTag> &tag) {
    if (tag.is_null() || !value.remove(tag->get_value())) {
        return false;
    }
    emit_changed();
    return true;
}

void GameplayTagContainer::remove_all(const Ref<GameplayTagContainer> &other) {
    if (other.is_valid()) {
        const size_t before = value.size();
        value.remove_all(other->value);
        if (value.size() != before) {
            emit_changed();
        }
    }
}

void GameplayTagContainer::append(const Ref<GameplayTagContainer> &other) {
    if (other.is_valid()) {
        const size_t before = value.size();
        value.append(other->value);
        if (value.size() != before) {
            emit_changed();
        }
    }
}

void GameplayTagContainer::append_matching(const Ref<GameplayTagContainer> &source,
        const Ref<GameplayTagContainer> &required) {
    if (source.is_valid() && required.is_valid()) {
        const size_t before = value.size();
        value.append_matching(source->value, required->value);
        if (value.size() != before) {
            emit_changed();
        }
    }
}

void GameplayTagContainer::clear() {
    if (!value.empty()) {
        value.clear();
        emit_changed();
    }
}

bool GameplayTagContainer::has(const Ref<GameplayTag> &tag) const {
    return tag.is_valid() && value.has(tag->get_value());
}

bool GameplayTagContainer::has_exact(const Ref<GameplayTag> &tag) const {
    return tag.is_valid() && value.has_exact(tag->get_value());
}

bool GameplayTagContainer::has_any(const Ref<GameplayTagContainer> &required) const {
    return required.is_valid() && value.has_any(required->value);
}

bool GameplayTagContainer::has_all(const Ref<GameplayTagContainer> &required) const {
    return required.is_valid() && value.has_all(required->value);
}

bool GameplayTagContainer::has_any_exact(const Ref<GameplayTagContainer> &required) const {
    return required.is_valid() && value.has_any_exact(required->value);
}

bool GameplayTagContainer::has_all_exact(const Ref<GameplayTagContainer> &required) const {
    return required.is_valid() && value.has_all_exact(required->value);
}

Ref<GameplayTagContainer> GameplayTagContainer::filter(const Ref<GameplayTagContainer> &required) const {
    return from_value(required.is_valid() ? value.filter(required->value) : ContainerValue());
}

Ref<GameplayTagContainer> GameplayTagContainer::filter_exact(const Ref<GameplayTagContainer> &required) const {
    return from_value(required.is_valid() ? value.filter_exact(required->value) : ContainerValue());
}

Ref<GameplayTagContainer> GameplayTagContainer::with_parents() const { return from_value(value.with_parents()); }

bool GameplayTagContainer::equals(const Ref<GameplayTagContainer> &other) const {
    return other.is_valid() && value == other->value;
}

bool GameplayTagContainer::matches_query(const Ref<GameplayTagQuery> &query) const {
    return query.is_valid() && query->value.matches(value);
}

const ContainerValue &GameplayTagContainer::get_value() const { return value; }

void GameplayTagContainer::set_saved_names(const PackedStringArray &names) {
    ContainerValue restored;
    for (int64_t index = 0; index < names.size(); ++index) {
        const TagValue tag = read_tag_name(StringName(names[index]));
        if (!tag.is_valid()) {
            UtilityFunctions::push_error("[Crimson] Invalid saved Gameplay Tag container. Correct its tag paths.");
            return;
        }
        restored.add(tag);
    }
    if (restored.tags() != value.tags()) {
        value = std::move(restored);
        emit_changed();
    }
}

PackedStringArray GameplayTagContainer::get_saved_names() const {
    PackedStringArray result;
    for (const TagValue &tag : value.tags()) {
        result.append(String(tag.name().c_str()));
    }
    return result;
}

void GameplayTagQuery::_bind_methods() {
    ClassDB::bind_static_method("GameplayTagQuery", D_METHOD("any_tags", "terms", "exact"), &GameplayTagQuery::any_tags, DEFVAL(false));
    ClassDB::bind_static_method("GameplayTagQuery", D_METHOD("all_tags", "terms", "exact"), &GameplayTagQuery::all_tags, DEFVAL(false));
    ClassDB::bind_static_method("GameplayTagQuery", D_METHOD("no_tags", "terms", "exact"), &GameplayTagQuery::no_tags, DEFVAL(false));
    ClassDB::bind_static_method("GameplayTagQuery", D_METHOD("any_queries", "queries"), &GameplayTagQuery::any_queries);
    ClassDB::bind_static_method("GameplayTagQuery", D_METHOD("all_queries", "queries"), &GameplayTagQuery::all_queries);
    ClassDB::bind_static_method("GameplayTagQuery", D_METHOD("no_queries", "queries"), &GameplayTagQuery::no_queries);
    ClassDB::bind_method(D_METHOD("matches", "tags"), &GameplayTagQuery::matches);
}

Ref<GameplayTagQuery> GameplayTagQuery::make_tags(QueryValue::Match mode, const Ref<GameplayTagContainer> &terms, bool exact) {
    Ref<GameplayTagQuery> result;
    if (terms.is_valid()) {
        result.instantiate();
        result->value = QueryValue::tags(mode, terms->get_value(), exact);
    }
    return result;
}

Ref<GameplayTagQuery> GameplayTagQuery::make_group(QueryValue::Match mode, const TypedArray<GameplayTagQuery> &queries) {
    std::vector<QueryValue> children;
    children.reserve(static_cast<size_t>(queries.size()));
    for (int64_t index = 0; index < queries.size(); ++index) {
        const Ref<GameplayTagQuery> query = queries[index];
        if (query.is_null()) {
            return {};
        }
        children.push_back(query->value);
    }
    Ref<GameplayTagQuery> result;
    result.instantiate();
    result->value = QueryValue::group(mode, std::move(children));
    return result;
}

Ref<GameplayTagQuery> GameplayTagQuery::any_tags(const Ref<GameplayTagContainer> &terms, bool exact) {
    return make_tags(QueryValue::Match::ANY, terms, exact);
}

Ref<GameplayTagQuery> GameplayTagQuery::all_tags(const Ref<GameplayTagContainer> &terms, bool exact) {
    return make_tags(QueryValue::Match::ALL, terms, exact);
}

Ref<GameplayTagQuery> GameplayTagQuery::no_tags(const Ref<GameplayTagContainer> &terms, bool exact) {
    return make_tags(QueryValue::Match::NONE, terms, exact);
}

Ref<GameplayTagQuery> GameplayTagQuery::any_queries(const TypedArray<GameplayTagQuery> &queries) {
    return make_group(QueryValue::Match::ANY, queries);
}

Ref<GameplayTagQuery> GameplayTagQuery::all_queries(const TypedArray<GameplayTagQuery> &queries) {
    return make_group(QueryValue::Match::ALL, queries);
}

Ref<GameplayTagQuery> GameplayTagQuery::no_queries(const TypedArray<GameplayTagQuery> &queries) {
    return make_group(QueryValue::Match::NONE, queries);
}

bool GameplayTagQuery::matches(const Ref<GameplayTagContainer> &tags) const {
    return tags.is_valid() && value.matches(tags->get_value());
}

} // namespace crimson::gameplay_tags::bindings
