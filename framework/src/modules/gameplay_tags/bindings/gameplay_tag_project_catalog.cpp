// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag_names.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <utility>

namespace crimson::gameplay_tags::bindings {

using namespace godot;

namespace {

// Builds a catalog from saved declarations, reporting whether every entry was valid.
bool build_catalog(const PackedStringArray &definitions, CatalogValue &catalog) {
    bool valid = true;
    for (int64_t index = 0; index < definitions.size(); ++index) {
        const CharString bytes = definitions[index].utf8();
        if (!catalog.register_tag(std::string_view(bytes.get_data(), static_cast<size_t>(bytes.length()))).is_valid()) {
            valid = false;
        }
    }
    return valid;
}

// Reads persisted declarations, or an empty array in a new project.
PackedStringArray project_definitions() {
    return ProjectSettings::get_singleton()->get_setting(definitions_setting, PackedStringArray());
}

// Reads persisted redirects without exposing the dictionary to mutation.
Dictionary project_redirects() {
    return ProjectSettings::get_singleton()->get_setting(redirects_setting, Dictionary());
}

// Checks whether adding the path would recreate a retired tag as an explicit or implicit name.
bool has_redirect_prefix(const TagValue &tag, const Dictionary &redirects) {
    if (redirects.is_empty()) {
        return false;
    }
    for (TagValue part = tag; part.is_valid(); part = part.parent()) {
        if (redirects.has(canonical_tag_name(part))) {
            return true;
        }
    }
    return false;
}

// Rejects targets that would be lost, alias collisions, and redirect chains.
bool redirects_are_valid(const CatalogValue &catalog, const Dictionary &redirects) {
    const Array keys = redirects.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const Variant source_entry = keys[index];
        const Variant target_entry = redirects[source_entry];
        if ((source_entry.get_type() != Variant::STRING_NAME && source_entry.get_type() != Variant::STRING) ||
                (target_entry.get_type() != Variant::STRING_NAME && target_entry.get_type() != Variant::STRING)) {
            return false;
        }
        const StringName source_name = source_entry;
        const StringName target_name = target_entry;
        const TagValue source = parse_tag_name(source_name);
        const TagValue target = parse_tag_name(target_name);
        if (!source.is_valid() || !target.is_valid() || canonical_tag_name(source) != source_name ||
                canonical_tag_name(target) != target_name || catalog.find(source.name()).is_valid() ||
                !catalog.find(target.name()).is_valid() || redirects.has(target_name)) {
            return false;
        }
    }
    return true;
}

// Produces a stable project file independently of catalog edit order.
PackedStringArray declared_names(const CatalogValue &catalog) {
    PackedStringArray names;
    for (const TagValue &tag : catalog.tags(true)) {
        names.append(String(tag.name().c_str()));
    }
    return names;
}

// Saves both settings as one change and restores in-memory values if saving fails.
Error save_catalog(const CatalogValue &catalog, const Dictionary &redirects) {
    if (!redirects_are_valid(catalog, redirects)) {
        return ERR_BUSY;
    }
    auto *settings = ProjectSettings::get_singleton();
    const PackedStringArray previous_definitions = project_definitions();
    const Dictionary previous_redirects = project_redirects();
    settings->set_setting(definitions_setting, declared_names(catalog));
    settings->set_setting(redirects_setting, redirects);
    const Error result = settings->save();
    if (result != OK) {
        settings->set_setting(definitions_setting, previous_definitions);
        settings->set_setting(redirects_setting, previous_redirects);
    }
    return result;
}

// Replaces a matched path prefix with the destination path.
std::string moved_name(const TagValue &tag, const TagValue &origin, const TagValue &destination) {
    return destination.name() + tag.name().substr(origin.name().size());
}

// Rejects project-setting mutations outside a Godot editor run.
bool can_author_project() { return Engine::get_singleton()->is_editor_hint(); }

} // namespace

void GameplayTagCatalog::_bind_methods() {
    ClassDB::bind_static_method("GameplayTagCatalog", D_METHOD("project"), &GameplayTagCatalog::project);
    ClassDB::bind_static_method("GameplayTagCatalog", D_METHOD("add_project_tag", "name"), &GameplayTagCatalog::add_project_tag);
    ClassDB::bind_static_method("GameplayTagCatalog", D_METHOD("rename_project_tag", "old_name", "new_name", "include_children"),
            &GameplayTagCatalog::rename_project_tag, DEFVAL(false));
    ClassDB::bind_static_method("GameplayTagCatalog", D_METHOD("remove_project_tag", "name"), &GameplayTagCatalog::remove_project_tag);
    ClassDB::bind_method(D_METHOD("register_tag", "name"), &GameplayTagCatalog::register_tag);
    ClassDB::bind_method(D_METHOD("find", "name"), &GameplayTagCatalog::find);
    ClassDB::bind_method(D_METHOD("is_declared", "tag"), &GameplayTagCatalog::is_declared);
    ClassDB::bind_method(D_METHOD("get_tags", "declared_only"), &GameplayTagCatalog::get_tags, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("children", "tag"), &GameplayTagCatalog::children);
    ClassDB::bind_method(D_METHOD("descendants", "tag"), &GameplayTagCatalog::descendants);
}

Ref<GameplayTagCatalog> GameplayTagCatalog::project() {
    Ref<GameplayTagCatalog> catalog;
    catalog.instantiate();
    const bool valid_definitions = build_catalog(project_definitions(), catalog->value);
    const Dictionary redirects = project_redirects();
    if (valid_definitions && redirects_are_valid(catalog->value, redirects)) {
        catalog->redirects = redirects.duplicate();
    }
    return catalog;
}

Error GameplayTagCatalog::add_project_tag(const StringName &name) {
    if (!can_author_project()) {
        return ERR_UNAUTHORIZED;
    }
    const TagValue tag = parse_tag_name(name);
    if (!tag.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    CatalogValue catalog;
    if (!build_catalog(project_definitions(), catalog)) {
        return ERR_INVALID_DATA;
    }
    const Dictionary redirects = project_redirects();
    if (catalog.is_declared(tag) || has_redirect_prefix(tag, redirects)) {
        return ERR_ALREADY_EXISTS;
    }
    catalog.register_tag(tag.name());
    return save_catalog(catalog, redirects);
}

Error GameplayTagCatalog::rename_project_tag(const StringName &old_name, const StringName &new_name,
        bool include_children) {
    if (!can_author_project()) {
        return ERR_UNAUTHORIZED;
    }
    const TagValue origin = parse_tag_name(old_name);
    const TagValue destination = parse_tag_name(new_name);
    if (!origin.is_valid() || !destination.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    CatalogValue current;
    if (!build_catalog(project_definitions(), current)) {
        return ERR_INVALID_DATA;
    }
    if (!current.find(origin.name()).is_valid()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (origin == destination) {
        return OK;
    }
    if (destination.matches(origin)) {
        return ERR_INVALID_PARAMETER;
    }
    const auto descendants = current.descendants(origin);
    if (!include_children && !descendants.empty()) {
        return ERR_BUSY;
    }

    CatalogValue next;
    std::vector<TagValue> moved;
    for (const TagValue &tag : current.tags(true)) {
        if (tag.matches(origin)) {
            moved.push_back(tag);
        } else {
            next.register_tag(tag.name());
        }
    }
    const Dictionary previous_redirects = project_redirects();
    if (next.find(destination.name()).is_valid() || has_redirect_prefix(destination, previous_redirects)) {
        return ERR_ALREADY_EXISTS;
    }
    for (const TagValue &tag : moved) {
        next.register_tag(moved_name(tag, origin, destination));
    }

    Dictionary redirects = previous_redirects.duplicate();
    const Array previous_keys = previous_redirects.keys();
    for (int64_t index = 0; index < previous_keys.size(); ++index) {
        const StringName key = previous_keys[index];
        const TagValue target = parse_tag_name(previous_redirects[key]);
        if (target.matches(origin)) {
            redirects[key] = StringName(moved_name(target, origin, destination).c_str());
        }
    }
    redirects[canonical_tag_name(origin)] = canonical_tag_name(destination);
    for (const TagValue &descendant : descendants) {
        redirects[canonical_tag_name(descendant)] = StringName(moved_name(descendant, origin, destination).c_str());
    }
    return save_catalog(next, redirects);
}

Error GameplayTagCatalog::remove_project_tag(const StringName &name) {
    if (!can_author_project()) {
        return ERR_UNAUTHORIZED;
    }
    const TagValue tag = parse_tag_name(name);
    if (!tag.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    CatalogValue current;
    if (!build_catalog(project_definitions(), current)) {
        return ERR_INVALID_DATA;
    }
    if (!current.is_declared(tag)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!current.children(tag).empty()) {
        return ERR_BUSY;
    }
    CatalogValue next;
    for (const TagValue &declared : current.tags(true)) {
        if (declared != tag) {
            next.register_tag(declared.name());
        }
    }
    return save_catalog(next, project_redirects());
}

Ref<GameplayTag> GameplayTagCatalog::register_tag(const StringName &name) {
    const TagValue tag = parse_tag_name(name);
    if (has_redirect_prefix(tag, redirects)) {
        return {};
    }
    return GameplayTag::from_value(value.register_tag(tag.name()));
}

Ref<GameplayTag> GameplayTagCatalog::find(const StringName &name) const {
    const TagValue tag = resolve_tag_name(parse_tag_name(name), redirects);
    return GameplayTag::from_value(value.find(tag.name()));
}

bool GameplayTagCatalog::is_declared(const Ref<GameplayTag> &tag) const {
    return tag.is_valid() && value.is_declared(tag->get_value());
}

TypedArray<GameplayTag> GameplayTagCatalog::get_tags(bool declared_only) const {
    return wrap_tags(value.tags(declared_only));
}

TypedArray<GameplayTag> GameplayTagCatalog::children(const Ref<GameplayTag> &tag) const {
    return tag.is_valid() ? wrap_tags(value.children(tag->get_value())) : TypedArray<GameplayTag>();
}

TypedArray<GameplayTag> GameplayTagCatalog::descendants(const Ref<GameplayTag> &tag) const {
    return tag.is_valid() ? wrap_tags(value.descendants(tag->get_value())) : TypedArray<GameplayTag>();
}

void register_gameplay_tag_bindings(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(GameplayTag);
    GDREGISTER_CLASS(GameplayTagContainer);
    GDREGISTER_CLASS(GameplayTagCatalog);
    GDREGISTER_CLASS(GameplayTagQuery);

    auto *settings = ProjectSettings::get_singleton();
    if (!settings->has_setting(definitions_setting)) {
        settings->set_setting(definitions_setting, PackedStringArray());
    }
    settings->set_initial_value(definitions_setting, PackedStringArray());
    if (!settings->has_setting(redirects_setting)) {
        settings->set_setting(redirects_setting, Dictionary());
    }
    settings->set_initial_value(redirects_setting, Dictionary());
}

} // namespace crimson::gameplay_tags::bindings
