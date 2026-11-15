// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_tag_picker.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/gameplay_tags/bindings/gameplay_tag_resources.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/menu_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>

namespace crimson::gameplay_tags {
namespace {

using namespace godot;
using TagResource = bindings::GameplayTag;
using Catalog = bindings::GameplayTagCatalog;
namespace ui = crimson::editor_ui;

/// Return a tag's path through its immediate parent.
String parent_path(const String &path) {
    const int64_t separator = path.rfind(".");
    return separator < 0 ? String() : path.substr(0, separator);
}

/// Match all search terms against the full, canonical tag path.
bool matches_search(const String &path, const PackedStringArray &terms) {
    for (const String &term : terms) {
        if (!path.contains(term)) {
            return false;
        }
    }
    return true;
}

/// Report catalog write failures in language that gives the editor user a next step.
String catalog_error(Error error) {
    switch (error) {
        case ERR_INVALID_PARAMETER: return "Use dot-separated segments with letters, digits, and underscores.";
        case ERR_ALREADY_EXISTS: return "A tag or rename redirect already uses that name.";
        case ERR_DOES_NOT_EXIST: return "The project catalog no longer declares this tag.";
        case ERR_BUSY:
        case ERR_UNAVAILABLE: return "A child or rename redirect still needs this tag.";
        default: return String("Couldn't save the gameplay tag catalog. Godot error ") + String::num_int64(error) + ".";
    }
}

/// Return keyboard focus after the Inspector has repainted the edited chips.
void restore_editor_focus(uint64_t preferred, uint64_t fallback) {
    for (uint64_t id : {preferred, fallback}) {
        auto *control = Object::cast_to<Control>(ObjectDB::get_instance(ObjectID(id)));
        if (control && control->is_visible_in_tree() && !control->is_queued_for_deletion()) {
            control->grab_focus();
            return;
        }
    }
}

} // namespace

void GameplayTagEditorPicker::configure() {
    hide();
    set_name("GameplayTagPicker");
    set_title("Gameplay Tag Picker");
    set_accessibility_name("Gameplay Tag Picker");
    set_transient(true);
    set_transient_to_focused(true);
    set_exclusive(true);
    set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    set_theme(ui::create_theme(this, EditorInterface::get_singleton()->get_editor_scale()));
    ui::embed_editor_popups(this);
    set_min_size(Vector2i(ui::pixels(this, 360), ui::pixels(this, 300)));

    auto *shell = ui::surface_panel(this, ui::Surface::CANVAS);
    shell->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    auto *padding = ui::inset(shell, ui::spacing::lg, ui::spacing::lg);
    auto *workbench = memnew(VBoxContainer);
    padding->add_child(workbench);
    workbench->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::md));

    auto *toolbar = memnew(HBoxContainer);
    workbench->add_child(toolbar);
    toolbar->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::sm));
    search = ui::search_input(toolbar, "Search tags", "Find tags whose full path matches every search term.");
    search->set_name("GameplayTagSearch");
    search->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    search->connect("text_changed", callable_mp(this, &GameplayTagEditorPicker::filter_changed));
    search->connect("text_submitted", callable_mp(this, &GameplayTagEditorPicker::focus_results));

    auto *create = ui::button(toolbar, "New Tag", "Declare a tag in this project's catalog.",
            callable_mp(this, &GameplayTagEditorPicker::run_command).bind(NEW_ROOT));
    create->set_name("GameplayTagNew");
    options = ui::menu(toolbar, "Options", "Manage the selected catalog tag and change the tree view.");
    options->set_name("GameplayTagOptions");
    PopupMenu *menu = options->get_popup();
    menu->add_item("Add Child Tag", NEW_CHILD);
    menu->add_item("Rename Tag", RENAME);
    menu->add_item("Delete Tag", DELETE_TAG);
    menu->add_item("Copy Tag Name", COPY_NAME);
    menu->add_separator();
    menu->add_item("Expand All", EXPAND_ALL);
    menu->add_item("Collapse All", COLLAPSE_ALL);
    menu->add_item("Clear Selection", CLEAR_SELECTION);
    menu->connect("about_to_popup", callable_mp(this, &GameplayTagEditorPicker::update_options));
    menu->connect("id_pressed", callable_mp(this, &GameplayTagEditorPicker::run_command));

    auto *results = ui::surface_panel(workbench, ui::Surface::SIDEBAR);
    results->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    auto *tree_padding = ui::inset(results, ui::spacing::xs, ui::spacing::xs);
    tree = memnew(Tree);
    tree_padding->add_child(tree);
    tree->set_name("GameplayTagTree");
    tree->set_accessibility_name("Project gameplay tags");
    tree->set_hide_root(true);
    tree->set_select_mode(Tree::SELECT_ROW);
    tree->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    tree->set_column_clip_content(0, true);
    const double scale = tree->get_theme_default_base_scale();
    tree->add_theme_stylebox_override("cursor", ui::rounded_box(Color(0, 0, 0, 0),
            ui::color(ui::Surface::ACCENT), 0, 0, scale));
    tree->connect("item_edited", callable_mp(this, &GameplayTagEditorPicker::item_edited));
    tree->connect("item_activated", callable_mp(this, &GameplayTagEditorPicker::item_activated));
    tree->connect("item_collapsed", callable_mp(this, &GameplayTagEditorPicker::branch_toggled));

    auto *footer = memnew(HBoxContainer);
    workbench->add_child(footer);
    footer->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::sm));
    status = ui::label(footer, {}, ui::LabelStyle::MUTED);
    status->set_name("GameplayTagStatus");
    status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    ui::button(footer, "Cancel", "Discard the tag changes and close the picker.",
            callable_mp(this, &GameplayTagEditorPicker::cancel))->set_name("GameplayTagCancel");
    apply = ui::button(footer, "Apply", "Apply these tags to the inspected property.",
            callable_mp(this, &GameplayTagEditorPicker::apply_choices), ui::ButtonStyle::PRIMARY);
    apply->set_name("GameplayTagApply");

    catalog_prompt = ui::editor_modal(this, shell, {}, {});
    catalog_prompt->set_name("GameplayTagCatalogPrompt");
    path_entry = ui::text_input(catalog_prompt->body(), {}, "Enter the full dot-separated gameplay tag path.");
    path_entry->set_name("GameplayTagPath");
    path_entry->set_placeholder("state.control.stun");
    rename_children = ui::checkbox(catalog_prompt->body(), "Rename descendants",
            ui::CheckState::UNCHECKED, {}, "Move descendant paths under the new name too.");
    catalog_prompt->register_text_enter(path_entry);
    catalog_prompt->set_validator(callable_mp(this, &GameplayTagEditorPicker::perform_catalog_action));

    connect("close_requested", callable_mp(this, &GameplayTagEditorPicker::cancel));
    set_process_input(false);
}

void GameplayTagEditorPicker::open(const PackedStringArray &initial, bool is_multiple, const String &reveal,
        ObjectID source_id, ObjectID focus_id, ObjectID fallback_id, const Callable &on_submit) {
    finish(false);
    source = source_id;
    return_focus = focus_id;
    fallback_focus = fallback_id;
    submit = on_submit;
    multiple = is_multiple;
    choices = initial.duplicate();
    search->clear();
    set_title(multiple ? "Pick Gameplay Tags" : "Pick Gameplay Tag");
    apply->set_text(multiple ? "Apply Tags" : "Apply Tag");
    refresh_catalog();
    const auto catalog = Catalog::project();
    const Ref<TagResource> resolved = catalog->find(StringName(reveal));
    rebuild_tree(resolved.is_valid() ? String(resolved->get_tag_name()) : reveal);
    popup_centered_clamped(Vector2i(ui::pixels(this, 580), ui::pixels(this, 520)), 0.9);
    set_process_input(true);
    search->grab_focus();
}

void GameplayTagEditorPicker::dismiss_source(ObjectID source_id) {
    if (source == source_id) {
        finish(false);
    }
}

String GameplayTagEditorPicker::selected_path() const {
    const TreeItem *selected = tree->get_selected();
    return selected ? String(selected->get_metadata(0)) : String();
}

void GameplayTagEditorPicker::refresh_catalog() {
    const auto catalog = Catalog::project();
    catalog_paths.clear();
    for (const Variant &entry : catalog->get_tags()) {
        const Ref<TagResource> tag = entry;
        catalog_paths.insert(String(tag->get_tag_name()));
    }

    PackedStringArray resolved_choices;
    for (const String &path : choices) {
        const Ref<TagResource> resolved = catalog->find(StringName(path));
        const String current = resolved.is_valid() ? String(resolved->get_tag_name()) : path;
        if (!resolved_choices.has(current)) {
            resolved_choices.append(current);
        }
    }
    choices = resolved_choices;
}

bool GameplayTagEditorPicker::is_catalog_path(const String &path) const {
    return catalog_paths.find(path) != catalog_paths.end();
}

void GameplayTagEditorPicker::rebuild_tree(const String &reveal) {
    const String focus_path = reveal.is_empty() ? selected_path() : reveal;
    tree->clear();
    items.clear();
    TreeItem *root = tree->create_item();
    const auto terms = search->get_text().strip_edges().to_lower().split(" ", false);
    std::set<String> visible;
    std::set<String> shown_ancestors;
    for (const String &path : catalog_paths) {
        if (matches_search(path, terms)) {
            visible.insert(path);
            if (!terms.is_empty()) {
                for (String prefix = parent_path(path); !prefix.is_empty(); prefix = parent_path(prefix)) {
                    visible.insert(prefix);
                    shown_ancestors.insert(prefix);
                }
            }
        }
    }
    // Expand chosen ancestry on open even if the last invocation left that branch collapsed.
    for (const String &path : choices) {
        for (String prefix = parent_path(path); !prefix.is_empty(); prefix = parent_path(prefix)) {
            shown_ancestors.insert(prefix);
        }
        if (!is_catalog_path(path) && matches_search(path, terms)) {
            visible.insert(path);
        }
    }
    for (String prefix = parent_path(reveal); !prefix.is_empty(); prefix = parent_path(prefix)) {
        shown_ancestors.insert(prefix);
    }
    for (const String &path : visible) {
        const bool orphan = !is_catalog_path(path);
        const auto parent = items.find(parent_path(path));
        auto *item = tree->create_item(!orphan && parent != items.end() ? parent->second : root);
        item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
        item->set_editable(0, true);
        const int64_t last_period = path.rfind(".");
        item->set_text(0, orphan || last_period < 0 ? path : path.substr(last_period + 1));
        item->set_metadata(0, path);
        item->set_tooltip_text(0, orphan ? path + String("\nThis tag is missing from the project catalog.") : path);
        if (orphan) {
            item->set_custom_color(0, ui::color(ui::Surface::MUTED));
        }
        items.emplace(path, item);
    }
    for (const auto &[path, item] : items) {
        const auto previous = branch_collapsed.find(path);
        if (item->get_first_child() && shown_ancestors.find(path) == shown_ancestors.end() &&
                previous != branch_collapsed.end()) {
            item->set_collapsed(previous->second);
        }
    }
    if (const auto focused = items.find(focus_path); focused != items.end()) {
        focused->second->select(0);
        tree->scroll_to_item(focused->second);
    }
    update_choices();
}

void GameplayTagEditorPicker::update_choices() {
    std::set<String> checked_paths;
    for (const String &path : choices) {
        checked_paths.insert(path);
        if (is_catalog_path(path)) {
            for (String ancestor = parent_path(path); !ancestor.is_empty(); ancestor = parent_path(ancestor)) {
                checked_paths.insert(ancestor);
            }
        }
    }
    for (const auto &[path, item] : items) {
        item->set_checked(0, checked_paths.find(path) != checked_paths.end());
    }
    update_status();
}

void GameplayTagEditorPicker::update_status() {
    if (items.empty()) {
        status->set_text(search->get_text().strip_edges().is_empty() ?
                "Add a tag to populate the project catalog." : "No tags match your search.");
        return;
    }
    const int64_t count = choices.size();
    status->set_text(count == 1 ? "1 tag selected" : String::num_int64(count) + " tags selected");
}

void GameplayTagEditorPicker::filter_changed(const String &) {
    rebuild_tree();
}

void GameplayTagEditorPicker::focus_results(const String &) {
    if (items.empty()) {
        return;
    }
    if (!tree->get_selected()) {
        tree->get_root()->get_first_child()->select(0);
    }
    tree->grab_focus();
}

void GameplayTagEditorPicker::branch_toggled(Object *item_object) {
    auto *item = Object::cast_to<TreeItem>(item_object);
    if (item && search->get_text().strip_edges().is_empty()) {
        branch_collapsed[String(item->get_metadata(0))] = item->is_collapsed();
    }
}

void GameplayTagEditorPicker::item_edited() {
    if (const auto *item = tree->get_edited()) {
        set_chosen(String(item->get_metadata(0)), item->is_checked(0));
    }
}

void GameplayTagEditorPicker::item_activated() {
    if (const auto *item = tree->get_selected()) {
        set_chosen(String(item->get_metadata(0)), !item->is_checked(0));
    }
}

void GameplayTagEditorPicker::set_chosen(const String &path, bool chosen) {
    if (chosen) {
        select_tag(path);
    } else {
        unselect_tag(path);
    }
    update_choices();
}

void GameplayTagEditorPicker::select_tag(const String &path) {
    if (!is_catalog_path(path)) {
        return;
    }
    if (!multiple) {
        choices.clear();
    }
    if (!choices.has(path)) {
        choices.append(path);
    }
    for (String ancestor = parent_path(path); !ancestor.is_empty(); ancestor = parent_path(ancestor)) {
        choices.erase(ancestor);
    }
}

void GameplayTagEditorPicker::unselect_tag(const String &path) {
    choices.erase(path);
    if (!is_catalog_path(path)) {
        return;
    }

    const String parent = parent_path(path);
    if (!parent.is_empty() && !choices.has(parent)) {
        // Only an explicitly selected direct sibling keeps the parent implicit.
        const bool has_direct_sibling = std::any_of(choices.begin(), choices.end(), [this, &parent](const String &choice) {
            return is_catalog_path(choice) && parent_path(choice) == parent;
        });
        if (!has_direct_sibling) {
            choices.append(parent);
        }
    }

    const String prefix = path + String(".");
    for (int64_t index = choices.size(); index > 0; --index) {
        const String &choice = choices[index - 1];
        if (choice.begins_with(prefix) && is_catalog_path(choice)) {
            choices.remove_at(index - 1);
        }
    }
}

void GameplayTagEditorPicker::apply_choices() {
    const Callable commit = submit;
    if (commit.is_valid() && bool(commit.call(choices))) {
        finish(true);
    } else {
        status->set_text("This property changed while the picker was open. Reopen it to edit the current value.");
    }
}

void GameplayTagEditorPicker::cancel() {
    if (catalog_prompt->is_open()) {
        catalog_prompt->cancel();
    } else {
        finish(true);
    }
}

void GameplayTagEditorPicker::finish(bool restore_focus) {
    if (catalog_prompt->is_open()) {
        catalog_prompt->close();
    }
    set_process_input(false);
    hide();
    if (restore_focus) {
        callable_mp_static(&restore_editor_focus)
                .bind(static_cast<uint64_t>(return_focus), static_cast<uint64_t>(fallback_focus)).call_deferred();
    }
    source = ObjectID();
    return_focus = ObjectID();
    fallback_focus = ObjectID();
    submit = {};
}

void GameplayTagEditorPicker::_input(const Ref<InputEvent> &event) {
    if (is_visible() && !catalog_prompt->is_open() &&
            (event->is_action_pressed("ui_cancel") || event->is_action_pressed("ui_close_dialog"))) {
        set_input_as_handled();
        cancel();
    }
}

void GameplayTagEditorPicker::update_options() {
    PopupMenu *menu = options->get_popup();
    const String chosen_path = selected_path();
    const auto catalog = Catalog::project();
    const Ref<TagResource> tag = catalog->find(StringName(chosen_path));
    const bool declared = tag.is_valid() && catalog->is_declared(tag);
    menu->set_item_disabled(menu->get_item_index(NEW_CHILD), tag.is_null());
    menu->set_item_disabled(menu->get_item_index(RENAME), tag.is_null());
    menu->set_item_disabled(menu->get_item_index(DELETE_TAG), !declared ||
            !catalog->descendants(tag).is_empty());
    menu->set_item_disabled(menu->get_item_index(COPY_NAME), chosen_path.is_empty());
    menu->set_item_disabled(menu->get_item_index(CLEAR_SELECTION), choices.is_empty());
}

void GameplayTagEditorPicker::run_command(int64_t command) {
    switch (command) {
        case COPY_NAME:
            DisplayServer::get_singleton()->clipboard_set(selected_path());
            break;
        case EXPAND_ALL:
        case COLLAPSE_ALL:
            for (const auto &[path, item] : items) {
                if (item->get_first_child()) {
                    const bool collapse = command == COLLAPSE_ALL;
                    branch_collapsed[path] = collapse;
                    item->set_collapsed(collapse);
                }
            }
            break;
        case CLEAR_SELECTION:
            choices.clear();
            rebuild_tree();
            break;
        default:
            prompt_catalog(static_cast<Command>(command));
            break;
    }
}

void GameplayTagEditorPicker::prompt_catalog(Command action) {
    catalog_action = action;
    catalog_target = selected_path();
    const bool removing = action == DELETE_TAG;
    const auto catalog = Catalog::project();
    const auto target = catalog->find(StringName(catalog_target));
    path_entry->set_visible(!removing);
    rename_children->set_visible(action == RENAME && target.is_valid() && !catalog->descendants(target).is_empty());
    rename_children->set_pressed_no_signal(rename_children->is_visible() && !catalog->is_declared(target));
    path_entry->set_text(action == NEW_CHILD ? catalog_target + "." :
            action == RENAME ? catalog_target : String());
    catalog_prompt->set_destructive(removing);
    catalog_prompt->set_confirm_text(removing ? "Delete Tag" : action == RENAME ? "Rename Tag" : "Add Tag");
    const String title = removing ? String("Delete ") + catalog_target + "?" :
            action == RENAME ? "Rename Gameplay Tag" : "New Gameplay Tag";
    const String help = removing ?
            "Saved fields that use this tag may show it as missing. You can restore the name by declaring it again." :
            action == RENAME ? "Saved values using the old name resolve through a project redirect." : String();
    catalog_prompt->open(title, help, removing ? nullptr : path_entry);
    if (!removing) {
        path_entry->set_caret_column(path_entry->get_text().length());
    }
}

bool GameplayTagEditorPicker::perform_catalog_action() {
    const StringName name(path_entry->get_text());
    Error result;
    if (catalog_action == DELETE_TAG) {
        result = Catalog::remove_project_tag(StringName(catalog_target));
    } else if (catalog_action == RENAME) {
        result = Catalog::rename_project_tag(StringName(catalog_target), name, rename_children->is_pressed());
    } else {
        result = Catalog::add_project_tag(name);
    }
    if (result != OK) {
        catalog_prompt->set_validation_message(catalog_error(result));
        return false;
    }
    const auto catalog = Catalog::project();
    const Ref<TagResource> changed = catalog->find(name);
    const String reveal = catalog_action != DELETE_TAG && changed.is_valid() ?
            String(changed->get_tag_name()) : parent_path(catalog_target);
    refresh_catalog();
    search->clear();
    rebuild_tree(reveal);
    return true;
}

} // namespace crimson::gameplay_tags

#endif
