// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/attributes/editor/register_attributes_editor.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/attributes/ability_system_component.hpp"
#include "modules/attributes/attribute_codegen.hpp"
#include "modules/attributes/attribute_registry.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/undo_redo.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <string>

namespace crimson::attributes {
namespace {

using namespace godot;
namespace ui = crimson::editor_ui;

/// Output used by the Generate action and displayed after a successful write.
constexpr char generated_directory[] = "res://generated/gameplay_attributes";
/// Scene property backed by the stable IDs authored in the project settings tab.
constexpr char component_sets_property[] = "attribute_sets";

/// In-order identifiers backing the presentation selector.
constexpr const char *visibility_ids[] = {"authority", "owner", "observers"};

/// Read an optional field without coercing malformed project settings into a valid value.
String text_field(const Dictionary &record, const char *field) {
    const Variant value = record.get(field, Variant());
    if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
        return {};
    }
    return String(value);
}

/// Read a collection only when the corresponding data has the expected shape.
Array array_field(const Dictionary &record, const char *field) {
    const Variant value = record.get(field, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Select one record without trusting an invalid index or unexpected stored value.
Dictionary record_at(const Array &records, int64_t index) {
    if (index < 0 || index >= records.size()) {
        return {};
    }
    const Variant value = records[index];
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}

/// Describe both the authored identity and the display label when they differ.
String entry_title(const Dictionary &entry) {
    const String id = text_field(entry, "id");
    const String label = text_field(entry, "label");
    if (id.is_empty()) {
        return "Invalid entry";
    }
    return label.is_empty() ? id.capitalize() : label + String("  (") + id + String(")");
}

/// Store a display label only when the author provides one.
void store_label(Dictionary &record, const LineEdit &editor) {
    const String label = editor.get_text().strip_edges();
    if (label.is_empty()) {
        record.erase("label");
    } else {
        record["label"] = label;
    }
}

/// Populate a stock selectable list while honoring the current editor draft selection.
void fill_list(ItemList *list, const Array &records, int64_t selected) {
    list->clear();
    for (int64_t index = 0; index < records.size(); ++index) {
        const Dictionary entry = record_at(records, index);
        const int32_t row = list->add_item(entry_title(entry));
        list->set_item_tooltip(row, text_field(entry, "id"));
    }
    if (selected >= 0 && selected < records.size()) {
        list->select(static_cast<int32_t>(selected));
    }
}

/// Own the settings tab, an editable copy of the registry, and its pending commands.
class GameplayAttributesPanel final : public Control {
    GDCLASS(GameplayAttributesPanel, Control)

    /// Selection-aware commands share the panel's single modal.
    enum class Action { NONE, ADD_SET, ADD_ATTRIBUTE, REMOVE_SET, REMOVE_ATTRIBUTE, DISCARD };

    /// Last authoritative project-settings value loaded or saved by this tab.
    Dictionary saved;
    /// Detached working state, with selected field edits applied as the selection changes.
    Dictionary draft;
    /// Current set and attribute indices, or -1 for no selection.
    int64_t selected_set = -1;
    int64_t selected_attribute = -1;
    /// True while the selected fields contain an edit that has not reached the draft.
    bool fields_changed = false;
    /// Intent pending inside the editor modal.
    Action action = Action::NONE;
    /// Sets and attributes use stock list keyboard navigation.
    ItemList *set_list = nullptr;
    ItemList *attribute_list = nullptr;
    /// The selected set and attribute forms, shown only when their records exist.
    VBoxContainer *set_form = nullptr;
    VBoxContainer *attribute_form = nullptr;
    /// Empty sidebars explain the next available action.
    Label *empty_sets = nullptr;
    Label *empty_attributes = nullptr;
    /// Authoring controls for the selected records.
    LineEdit *set_id = nullptr;
    LineEdit *set_label = nullptr;
    LineEdit *attribute_id = nullptr;
    LineEdit *attribute_label = nullptr;
    ui::Picker *visibility = nullptr;
    /// Commands whose availability depends on a selection or pending edit.
    Button *add_set = nullptr;
    Button *remove_set = nullptr;
    Button *add_attribute = nullptr;
    Button *remove_attribute = nullptr;
    Button *discard = nullptr;
    /// Inline feedback belongs to the settings tab.
    Label *status = nullptr;
    /// Reusable scoped modal and its creation ID field.
    ui::EditorModal *prompt = nullptr;
    Control *id_prompt_row = nullptr;
    LineEdit *id_prompt = nullptr;

    /// Read the current draft set.
    Dictionary current_set() const { return record_at(array_field(draft, "sets"), selected_set); }

    /// Print one actionable status in the tab without logging repeated form validation.
    void show_status(const String &message, bool error = false) {
        status->set_text(message);
        status->add_theme_color_override("font_color", ui::color(error ? ui::Surface::DANGER : ui::Surface::MUTED));
    }

    /// Enable draft actions based on the current edit and selection.
    void refresh_actions() {
        remove_set->set_disabled(selected_set < 0);
        add_attribute->set_disabled(selected_set < 0);
        remove_attribute->set_disabled(selected_attribute < 0);
        discard->set_disabled(!fields_changed && draft == saved);
    }

    /// Describe the next operation when the active set has no attribute rows.
    void show_empty_attributes(bool empty) {
        empty_attributes->set_text(selected_set < 0 ?
                "Select a set to author its attributes." : "No attributes in this set yet.");
        empty_attributes->set_visible(empty);
        attribute_list->set_visible(!empty);
    }

    /// Mark a live field edit without writing to Project Settings.
    void mark_fields_changed() {
        fields_changed = true;
        show_status("Unsaved changes");
        refresh_actions();
    }

    /// Receive a text edit from one of the selected fields.
    void text_changed(const String &) { mark_fields_changed(); }

    /// Receive a selection from the replication visibility control.
    void visibility_changed(int64_t) { mark_fields_changed(); }

    /// Read the selected labels and replication choice into the detached draft.
    void store_fields() {
        if (!fields_changed || selected_set < 0) {
            return;
        }

        Dictionary set = current_set();
        if (selected_attribute >= 0) {
            Array attributes = array_field(set, "attributes");
            Dictionary attribute = record_at(attributes, selected_attribute);
            store_label(attribute, *attribute_label);
            const int selected_visibility = visibility->get_selected_index();
            if (selected_visibility >= 0 && selected_visibility < 3) {
                attribute["visibility"] = String(visibility_ids[selected_visibility]);
            }
            attributes[selected_attribute] = attribute;
            set["attributes"] = attributes;
            attribute_list->set_item_text(static_cast<int32_t>(selected_attribute), entry_title(attribute));
        }
        store_label(set, *set_label);
        Array sets = array_field(draft, "sets");
        sets[selected_set] = set;
        draft["sets"] = sets;
        set_list->set_item_text(static_cast<int32_t>(selected_set), entry_title(set));
        fields_changed = false;
        refresh_actions();
    }

    /// Read the visible choices in their persisted order.
    PackedStringArray visibility_labels() const {
        PackedStringArray labels;
        labels.push_back("Authority only");
        labels.push_back("Owner");
        labels.push_back("Observers");
        return labels;
    }

    /// Present the selected records without emitting edit signals.
    void refresh_forms() {
        const Dictionary set = current_set();
        const Dictionary attribute = record_at(array_field(set, "attributes"), selected_attribute);
        set_form->set_visible(selected_set >= 0);
        attribute_form->set_visible(selected_attribute >= 0);
        set_id->set_text(text_field(set, "id"));
        set_label->set_text(text_field(set, "label"));
        attribute_id->set_text(text_field(attribute, "id"));
        attribute_label->set_text(text_field(attribute, "label"));
        const String stored_visibility = text_field(attribute, "visibility");
        const int selected = stored_visibility == "owner" ? 1 : stored_visibility == "observers" ? 2 : 0;
        visibility->set_items(visibility_labels(), selected);
    }

    /// Rebuild lists after structural changes and expose the next useful action.
    void refresh_lists() {
        const Array sets = array_field(draft, "sets");
        const Array attributes = array_field(current_set(), "attributes");
        fill_list(set_list, sets, selected_set);
        fill_list(attribute_list, attributes, selected_attribute);
        empty_sets->set_visible(sets.is_empty());
        show_empty_attributes(attributes.is_empty());
        refresh_forms();
        refresh_actions();
    }

    /// Load the latest settings only when the tab has no draft to preserve.
    void read_project() {
        saved = project_attribute_registry();
        draft = saved.duplicate(true);
        const Array sets = array_field(draft, "sets");
        selected_set = sets.is_empty() ? -1 : 0;
        selected_attribute = array_field(current_set(), "attributes").is_empty() ? -1 : 0;
        fields_changed = false;
        show_status({});
        refresh_lists();
    }

    /// Refresh externally loaded settings on the next visit to an idle tab.
    void shown() {
        if (is_visible_in_tree() && !fields_changed && draft == saved && project_attribute_registry() != saved) {
            read_project();
        }
    }

    /// Select a set, preserving any pending edits before switching.
    void select_set(int64_t index) {
        store_fields();
        selected_set = index;
        const Array attributes = array_field(current_set(), "attributes");
        selected_attribute = attributes.is_empty() ? -1 : 0;
        fill_list(attribute_list, attributes, selected_attribute);
        show_empty_attributes(attributes.is_empty());
        refresh_forms();
        refresh_actions();
    }

    /// Select an attribute after storing the previous row's pending fields.
    void select_attribute(int64_t index) {
        store_fields();
        selected_attribute = index;
        refresh_forms();
        refresh_actions();
    }

    /// Prepare a copy containing the requested addition for shared registry validation.
    Dictionary with_new_entry(const String &id) const {
        Dictionary next = draft.duplicate(true);
        Array sets = array_field(next, "sets");
        Dictionary entry;
        entry["id"] = id;
        if (action == Action::ADD_SET) {
            entry["attributes"] = Array();
            sets.push_back(entry);
        } else {
            Dictionary set = record_at(sets, selected_set);
            Array attributes = array_field(set, "attributes");
            entry["visibility"] = String("authority");
            attributes.push_back(entry);
            set["attributes"] = attributes;
            sets[selected_set] = set;
        }
        next["sets"] = sets;
        return next;
    }

    /// Keep the creation modal open when the shared schema rejects an entry.
    bool validate_prompt() {
        if (action != Action::ADD_SET && action != Action::ADD_ATTRIBUTE) {
            return true;
        }
        AttributeRegistry validated;
        String error;
        if (!parse_attribute_registry(with_new_entry(id_prompt->get_text().strip_edges()), validated, error)) {
            prompt->set_validation_message(error);
            return false;
        }
        return true;
    }

    /// Open the same explicit ID dialog for sets and attributes.
    void open_add(bool is_set) {
        if (!is_set && selected_set < 0) {
            return;
        }
        store_fields();
        action = is_set ? Action::ADD_SET : Action::ADD_ATTRIBUTE;
        id_prompt_row->show();
        id_prompt->clear();
        prompt->set_confirm_text(is_set ? "Add Set" : "Add Attribute");
        prompt->set_destructive(false);
        prompt->open(is_set ? "Add Attribute Set" : "Add Attribute",
                is_set ? "Use a stable lowercase ID such as vitals." : "Use a stable lowercase ID such as health.", id_prompt);
    }

    /// Open a set creation prompt from the sidebar command.
    void open_add_set() { open_add(true); }

    /// Open an attribute creation prompt for the selected set.
    void open_add_attribute() { open_add(false); }

    /// Ask for confirmation while keeping unrelated draft values intact.
    void open_removal(bool is_set) {
        if (selected_set < 0 || (!is_set && selected_attribute < 0)) {
            return;
        }
        action = is_set ? Action::REMOVE_SET : Action::REMOVE_ATTRIBUTE;
        id_prompt_row->hide();
        prompt->set_confirm_text(is_set ? "Remove Set" : "Remove Attribute");
        prompt->set_destructive(true);
        const String target = is_set ? set_id->get_text() :
                set_id->get_text() + String(".") + attribute_id->get_text();
        prompt->open(String("Remove ") + target + String("?"),
                "The draft changes now. Saving removes the entry from Project Settings.");
    }

    /// Confirm removal of the selected set.
    void open_remove_set() { open_removal(true); }

    /// Confirm removal of the selected attribute.
    void open_remove_attribute() { open_removal(false); }

    /// Confirm restoring the last saved project configuration.
    void open_discard() {
        action = Action::DISCARD;
        id_prompt_row->hide();
        prompt->set_confirm_text("Discard Changes");
        prompt->set_destructive(true);
        prompt->open("Discard attribute edits?", "Restore the saved Project Settings.");
    }

    /// Apply the single confirmed draft operation.
    void accept_prompt() {
        const Action confirmed = action;
        action = Action::NONE;
        if (confirmed == Action::DISCARD) {
            read_project();
            if (selected_set >= 0) {
                set_list->grab_focus();
            } else {
                add_set->grab_focus();
            }
            return;
        }
        Control *next_focus = add_set;
        if (confirmed == Action::ADD_SET || confirmed == Action::ADD_ATTRIBUTE) {
            action = confirmed;
            draft = with_new_entry(id_prompt->get_text().strip_edges());
            action = Action::NONE;
            if (confirmed == Action::ADD_SET) {
                selected_set = array_field(draft, "sets").size() - 1;
                selected_attribute = -1;
                next_focus = add_attribute;
            } else {
                selected_attribute = array_field(current_set(), "attributes").size() - 1;
                next_focus = attribute_label;
            }
        } else if (confirmed == Action::REMOVE_SET) {
            Array sets = array_field(draft, "sets");
            sets.remove_at(selected_set);
            draft["sets"] = sets;
            selected_set = MIN(selected_set, sets.size() - 1);
            selected_attribute = array_field(current_set(), "attributes").is_empty() ? -1 : 0;
            if (selected_set >= 0) {
                next_focus = set_list;
            }
        } else if (confirmed == Action::REMOVE_ATTRIBUTE) {
            Array sets = array_field(draft, "sets");
            Dictionary set = current_set();
            store_label(set, *set_label);
            Array attributes = array_field(set, "attributes");
            attributes.remove_at(selected_attribute);
            set["attributes"] = attributes;
            sets[selected_set] = set;
            draft["sets"] = sets;
            selected_attribute = MIN(selected_attribute, attributes.size() - 1);
            next_focus = add_attribute;
            if (selected_attribute >= 0) {
                next_focus = attribute_list;
            }
        }
        fields_changed = false;
        show_status("Unsaved changes");
        refresh_lists();
        next_focus->grab_focus();
    }

    /// Cancel a scoped edit without touching the draft.
    void cancel_prompt() { action = Action::NONE; }

    /// Validate the whole draft and persist it before exposing it as project settings.
    bool save_draft(AttributeRegistry &validated) {
        store_fields();
        if (project_attribute_registry() != saved) {
            show_status("Project Settings changed outside this tab. Discard this draft to load the current registry.", true);
            discard->set_disabled(false);
            return false;
        }
        String error;
        if (!parse_attribute_registry(draft, validated, error)) {
            show_status(error, true);
            return false;
        }
        std::string symbol_error;
        if (!validate_attribute_header_symbols(validated, symbol_error)) {
            show_status(String::utf8(symbol_error.c_str()), true);
            return false;
        }
        if (save_project_attribute_registry(draft, error) != OK) {
            show_status(error, true);
            return false;
        }
        saved = project_attribute_registry();
        draft = saved.duplicate(true);
        refresh_actions();
        show_status("Gameplay Attributes saved.");
        return true;
    }

    /// Persist the authored registry without creating native source files.
    void save_action() {
        AttributeRegistry validated;
        save_draft(validated);
    }

    /// Generate from the validated draft after its project settings have been saved.
    void generate_action() {
        AttributeRegistry validated;
        if (!save_draft(validated)) {
            return;
        }
        String error;
        if (generate_attribute_headers(validated, generated_directory, error) != OK) {
            show_status(String("Settings saved. ") + error, true);
            return;
        }
        show_status(String("Generated C++ headers in ") + String(generated_directory) + String("."));
    }

    /// Add a field using the shared editor label, tooltip, and text input treatment.
    LineEdit *line_field(Node *owner, const StringName &name, const String &label,
            const String &help, bool read_only = false, const String &placeholder = {}) {
        auto *edit = ui::text_input(nullptr, {}, help,
                read_only ? ui::FieldState::READ_ONLY : ui::FieldState::EDITABLE, placeholder);
        edit->set_name(name);
        ui::field(owner, label, edit, help, 116);
        if (!read_only) {
            edit->connect("text_changed", callable_mp(this, &GameplayAttributesPanel::text_changed));
        }
        return edit;
    }

    /// Build the list area with its own stock scrolling and focus.
    ItemList *make_list(Node *owner, const StringName &name, const String &description, int height) {
        auto *list = memnew(ItemList);
        owner->add_child(list);
        list->set_name(name);
        list->set_accessibility_name(description);
        list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
        list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        list->set_custom_minimum_size(Vector2(0, ui::pixels(list, height)));
        list->set_focus_mode(Control::FOCUS_ALL);
        return list;
    }

    /// Present the set list on a quiet surface alongside the attribute form.
    void build_sets(HBoxContainer *body) {
        auto *sidebar = ui::surface_panel(body, ui::Surface::SIDEBAR);
        sidebar->set_custom_minimum_size(Vector2(ui::pixels(sidebar, 166), 0));
        auto *sets_panel = ui::field_group(ui::inset(sidebar, ui::spacing::md, ui::spacing::md));
        ui::label(sets_panel, "Attribute Sets", ui::LabelStyle::HEADING);
        add_set = ui::button(sets_panel, "Add Set", "Create a named attribute set.",
                callable_mp(this, &GameplayAttributesPanel::open_add_set));
        add_set->set_name("GameplayAttributesAddSet");
        empty_sets = ui::label(sets_panel, "No sets yet", ui::LabelStyle::MUTED);
        set_list = make_list(sets_panel, "GameplayAttributesSets", "Attribute sets", 72);
        set_list->connect("item_selected", callable_mp(this, &GameplayAttributesPanel::select_set));
        remove_set = ui::button(sets_panel, "Remove Set", "Remove the selected set and its attribute definitions.",
                callable_mp(this, &GameplayAttributesPanel::open_remove_set), ui::ButtonStyle::QUIET);
        remove_set->set_name("GameplayAttributesRemoveSet");
    }

    /// Present one reusable inspector form and a list of attributes in the active set.
    void build_attributes(HBoxContainer *body) {
        auto *scroll = memnew(ScrollContainer);
        body->add_child(scroll);
        scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
        scroll->set_follow_focus(true);
        auto *editor = ui::field_group(scroll);
        set_form = ui::field_group(editor);
        set_id = line_field(set_form, "GameplayAttributesSetId", "Set ID",
                "Stable project-wide identity. This is part of each qualified attribute ID.", true);
        set_label = line_field(set_form, "GameplayAttributesSetLabel", "Set Label",
                "Optional editor label. Leave blank to show a readable form of the set ID.", false, "Optional");
        auto *tools = memnew(HBoxContainer);
        editor->add_child(tools);
        auto *heading = ui::label(tools, "Attributes", ui::LabelStyle::HEADING);
        heading->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        add_attribute = ui::button(tools, "Add Attribute", "Create an attribute in the selected set.",
                callable_mp(this, &GameplayAttributesPanel::open_add_attribute));
        add_attribute->set_name("GameplayAttributesAddAttribute");
        remove_attribute = ui::button(tools, "Remove Attribute", "Remove the selected attribute definition.",
                callable_mp(this, &GameplayAttributesPanel::open_remove_attribute), ui::ButtonStyle::QUIET);
        remove_attribute->set_name("GameplayAttributesRemoveAttribute");
        empty_attributes = ui::label(editor, "Select a set to author its attributes.", ui::LabelStyle::MUTED);
        attribute_list = make_list(editor, "GameplayAttributesEntries", "Attributes in the selected set", 96);
        attribute_list->set_v_size_flags(Control::SIZE_FILL);
        attribute_list->connect("item_selected", callable_mp(this, &GameplayAttributesPanel::select_attribute));
        attribute_form = ui::field_group(editor);
        attribute_id = line_field(attribute_form, "GameplayAttributesId", "Attribute ID",
                "Stable identity within this set. The set ID and this ID form the runtime key.", true);
        attribute_label = line_field(attribute_form, "GameplayAttributesLabel", "Display Label",
                "Optional editor label. Leave blank to show a readable form of the attribute ID.", false, "Optional");
        visibility = ui::select(nullptr, visibility_labels(), 0,
                callable_mp(this, &GameplayAttributesPanel::visibility_changed));
        visibility->set_name("GameplayAttributesVisibility");
        ui::field(attribute_form, "Replication", visibility,
                "Choose if only the authority, the owner, or visible observers can read the value.", 116);
    }

protected:
    /// The tab implementation exposes no game-facing script bindings.
    static void _bind_methods() {}

public:
    /// Compose the tab after attaching it to the Godot Project Settings dialog.
    void configure() {
        set_theme(ui::create_theme(get_window(), EditorInterface::get_singleton()->get_editor_scale()));
        set_custom_minimum_size(Vector2(ui::pixels(this, 500), ui::pixels(this, 230)));
        auto *frame = ui::inset(this, ui::spacing::lg, ui::spacing::md);
        frame->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
        auto *workbench = ui::field_group(frame);
        auto *body = memnew(HBoxContainer);
        workbench->add_child(body);
        body->set_v_size_flags(Control::SIZE_EXPAND_FILL);
        body->add_theme_constant_override("separation", ui::pixels(body, ui::spacing::lg));
        build_sets(body);
        build_attributes(body);

        auto *footer = memnew(HBoxContainer);
        workbench->add_child(footer);
        status = ui::label(footer, {}, ui::LabelStyle::MUTED);
        status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        status->set_name("GameplayAttributesStatus");
        discard = ui::button(footer, "Discard Changes", "Restore the latest saved Project Settings.",
                callable_mp(this, &GameplayAttributesPanel::open_discard), ui::ButtonStyle::QUIET);
        discard->set_name("GameplayAttributesDiscard");
        auto *save = ui::button(footer, "Save", "Validate and save the draft to project.godot.",
                callable_mp(this, &GameplayAttributesPanel::save_action));
        save->set_name("GameplayAttributesSave");
        auto *generate = ui::button(footer, "Generate",
                "Save the validated registry and write C++ headers to res://generated/gameplay_attributes.",
                callable_mp(this, &GameplayAttributesPanel::generate_action), ui::ButtonStyle::PRIMARY);
        generate->set_name("GameplayAttributesGenerate");

        prompt = ui::editor_modal(this, frame, callable_mp(this, &GameplayAttributesPanel::accept_prompt),
                callable_mp(this, &GameplayAttributesPanel::cancel_prompt));
        prompt->set_name("GameplayAttributesPrompt");
        id_prompt = ui::text_input(nullptr, {}, "Required stable identity.", ui::FieldState::EDITABLE);
        id_prompt->set_name("GameplayAttributesPromptId");
        id_prompt_row = ui::field(prompt->body(), "ID", id_prompt, "Required stable identity.", 116);
        prompt->set_validator(callable_mp(this, &GameplayAttributesPanel::validate_prompt));
        prompt->register_text_enter(id_prompt);
        connect("visibility_changed", callable_mp(this, &GameplayAttributesPanel::shown));
        read_project();
    }
};

/// Select registered sets for a single component through Godot's property transaction.
class AbilitySystemAttributeSetsProperty final : public EditorProperty {
    GDCLASS(AbilitySystemAttributeSetsProperty, EditorProperty)

    /// Last value and editor catalog read from their respective owners.
    PackedStringArray rendered;
    Dictionary catalog;
    /// Available sets indexed by stable ID for display and selection.
    Dictionary available;
    /// State for the Godot Inspector and its standalone picker.
    bool locked = false;
    VBoxContainer *assigned = nullptr;
    ui::Picker *picker = nullptr;
    Label *help = nullptr;

    /// Read a snapshot of the scene property before making an edit.
    PackedStringArray read_sets() {
        auto *owner = get_edited_object();
        return owner ? PackedStringArray(owner->get(get_edited_property())) : PackedStringArray();
    }

    /// Use the editor's native undo history and Inspector change tracking for every edit.
    void write_sets(const PackedStringArray &next) {
        auto *owner = get_edited_object();
        if (locked || !owner || next == read_sets()) {
            return;
        }
        auto *history = EditorInterface::get_singleton()->get_editor_undo_redo();
        history->create_action("Set Ability System Attribute Sets", UndoRedo::MERGE_DISABLE, owner);
        emit_changed(get_edited_property(), next);
        history->commit_action();
        callable_mp(this, &AbilitySystemAttributeSetsProperty::_update_property).call_deferred();
    }

    /// Add a current project set and retain the selected order.
    void add_set(const StringName &selection) {
        const String id(selection);
        if (project_attribute_registry() != catalog || !available.has(id)) {
            _update_property();
            return;
        }
        PackedStringArray next = read_sets();
        if (!next.has(id)) {
            next.append(id);
            write_sets(next);
        }
    }

    /// Remove an assigned ID even when its definition has since been deleted.
    void remove_set(const String &id) {
        PackedStringArray next = read_sets();
        if (next.erase(id)) {
            write_sets(next);
        }
    }

    /// Render the component value and the catalog-derived alternatives.
    void refresh(bool forced) {
        if (!assigned || !is_inside_tree() || !get_edited_object()) {
            return;
        }
        const PackedStringArray value = read_sets();
        const Dictionary authored = project_attribute_registry();
        if (!forced && rendered == value && catalog == authored) {
            return;
        }
        auto *focused = get_viewport()->gui_get_focus_owner();
        auto *trigger = picker->get_trigger();
        const bool inside_rows = focused && assigned->is_ancestor_of(focused);
        rendered = value;
        catalog = authored;
        available.clear();

        AttributeRegistry registry;
        String error;
        const bool valid = parse_attribute_registry(catalog, registry, error);
        std::vector<ui::PickerOption> choices;
        if (valid) {
            for (const auto &set : registry.sets) {
                const String id = String::utf8(set.id.c_str());
                const String label = String::utf8(set.label.c_str());
                const String title = label.is_empty() ? id : label + String(" (") + id + String(")");
                available[id] = title;
                if (!rendered.has(id)) {
                    choices.push_back({StringName(id), title, {}});
                }
            }
        }

        while (assigned->get_child_count() > 0) {
            auto *old = assigned->get_child(0);
            assigned->remove_child(old);
            old->queue_free();
        }
        Control *first_remove = nullptr;
        for (const String &id : rendered) {
            auto *row = memnew(HBoxContainer);
            assigned->add_child(row);
            row->set_name("AssignedAttributeSet");
            const bool exists = available.has(id);
            auto *caption = ui::label(row, exists ? String(available[id]) : id + String(" (missing)"));
            caption->set_h_size_flags(SIZE_EXPAND_FILL);
            caption->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
            ui::tooltip(caption, id);
            auto *remove = ui::icon_button(row, "close", String("Remove attribute set ") + id + String("."),
                    callable_mp(this, &AbilitySystemAttributeSetsProperty::remove_set).bind(id), ui::ButtonStyle::QUIET);
            remove->set_name("RemoveAttributeSet");
            remove->set_accessibility_name(String("Remove attribute set ") + id);
            remove->set_disabled(locked);
            if (!locked && !first_remove) {
                first_remove = remove;
            }
        }

        ui::tooltip(trigger, choices.empty() && !available.is_empty() ? "All registered sets are assigned." :
                "Choose an attribute set registered in Project Settings.");
        picker->set_options(std::move(choices));
        picker->set_selected({});
        picker->set_state(locked ? ui::FieldState::READ_ONLY : ui::FieldState::EDITABLE);
        help->set_text(valid ? "Define sets on the Gameplay Attributes tab in Project Settings." : error);
        help->set_visible(available.is_empty());
        if (inside_rows || (focused == trigger && trigger->is_disabled())) {
            Control *next_focus = trigger->is_disabled() ? first_remove : trigger;
            if (next_focus) {
                next_focus->call_deferred("grab_focus");
            }
        }
    }

protected:
    /// The Inspector control exposes no game-facing bindings.
    static void _bind_methods() {}

public:
    /// Install focused Crimson controls below the native property label.
    void configure(const Ref<Theme> &theme) {
        set_name("AbilitySystemAttributeSetsProperty");
        auto *body = ui::field_group(nullptr);
        body->set_theme(theme);
        add_child(body);
        set_bottom_editor(body);
        assigned = ui::field_group(body);
        picker = ui::picker(body, "Add Attribute Set", callable_mp(this, &AbilitySystemAttributeSetsProperty::add_set), true);
        picker->set_name("AttributeSetPicker");
        picker->set_popup_minimum_width(240);
        picker->set_search_placeholder("Search attribute sets");
        picker->get_trigger()->set_name("AddAttributeSet");
        add_focusable(picker->get_trigger());
        help = ui::label(body, {}, ui::LabelStyle::MUTED);
        help->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        ProjectSettings::get_singleton()->connect("settings_changed",
                callable_mp(this, &AbilitySystemAttributeSetsProperty::_update_property));
    }

    /// Synchronize external edits, undo, redo, and project catalog updates.
    void _update_property() override { refresh(false); }

    /// Apply Godot's read-only mode to the picker and assigned rows.
    void _set_read_only(bool read_only) override {
        locked = read_only;
        refresh(true);
    }
};

/// Replace the raw PackedStringArray with a project-catalog picker on the ability component.
class AbilitySystemAttributesInspector final : public EditorInspectorPlugin {
    GDCLASS(AbilitySystemAttributesInspector, EditorInspectorPlugin)

    /// Share the Gameplay Attributes theme across inspector rows.
    Ref<Theme> theme;

protected:
    /// This editor-only plugin exposes no script bindings.
    static void _bind_methods() {}

public:
    /// Use the same Crimson controls and scale as Project Settings.
    void configure(const Ref<Theme> &value) { theme = value; }

    /// Match the concrete component and its subclasses.
    bool _can_handle(Object *owner) const override {
        return Object::cast_to<AbilitySystemComponent>(owner) != nullptr;
    }

    /// Keep the normal Inspector editor for every other property.
    bool _parse_property(Object *, Variant::Type type, const String &name, PropertyHint, const String &,
            BitField<PropertyUsageFlags>, bool) override {
        if (type != Variant::PACKED_STRING_ARRAY || name != component_sets_property) {
            return false;
        }
        auto *row = memnew(AbilitySystemAttributeSetsProperty);
        row->configure(theme);
        add_property_editor(name, row);
        return true;
    }
};

/// Register and retire the tab with the editor plugin lifecycle.
class GameplayAttributesEditorPlugin final : public EditorPlugin {
    GDCLASS(GameplayAttributesEditorPlugin, EditorPlugin)

    /// Tab owned by this plugin even while the Project Settings dialog is hidden.
    GameplayAttributesPanel *tab = nullptr;
    /// Native properties installed in the scene Inspector.
    Ref<AbilitySystemAttributesInspector> inspector;

protected:
    /// This plugin provides editor behavior through the engine lifecycle.
    static void _bind_methods() {}

    /// Add the tab once and explicitly remove its control before freeing it.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            tab = memnew(GameplayAttributesPanel);
            tab->set_name("Gameplay Attributes");
            add_control_to_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, tab);
            tab->configure();
            inspector.instantiate();
            inspector->configure(tab->get_theme());
            add_inspector_plugin(inspector);
        } else if (what == NOTIFICATION_EXIT_TREE && tab) {
            remove_inspector_plugin(inspector);
            inspector.unref();
            remove_control_from_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, tab);
            memdelete(tab);
            tab = nullptr;
        }
    }
};

} // namespace
} // namespace crimson::attributes

#endif

namespace crimson::attributes {

void register_attributes_editor(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(GameplayAttributesPanel);
        GDREGISTER_INTERNAL_CLASS(AbilitySystemAttributeSetsProperty);
        GDREGISTER_INTERNAL_CLASS(AbilitySystemAttributesInspector);
        GDREGISTER_INTERNAL_CLASS(GameplayAttributesEditorPlugin);
        godot::EditorPlugins::add_by_type<GameplayAttributesEditorPlugin>();
    }
#else
    (void)level;
#endif
}

void unregister_attributes_editor(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<GameplayAttributesEditorPlugin>();
    }
#else
    (void)level;
#endif
}

} // namespace crimson::attributes
