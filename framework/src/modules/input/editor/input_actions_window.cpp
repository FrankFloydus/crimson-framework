// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "input_actions_window.hpp"
#include "input_control_labels.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/input/input_sources.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/editor_file_dialog.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>

namespace crimson::input {
using namespace godot;
namespace ui = crimson::editor_ui;
namespace spacing = ui::spacing;

namespace {

// Friendly value names in the same order as the saved enum values.
constexpr const char *kind_names[] = {"Button", "1D Axis", "2D Vector", "Mouse Motion"};

// Describes a binding command without exposing a raw input event resource.
enum Preset { EMPTY, WASD, LEFT_STICK, RIGHT_STICK };

// Keep the common controls usable even when a physical device is unavailable.
enum DeviceChoice { KEYBOARD, MOUSE, PAD_BUTTON, PAD_AXIS };

// Attach a standard control to its owner for the lifetime of the window.
template <class T>
T *add(Node *parent, const char *name = nullptr) {
    T *control = memnew(T);
    if (name) {
        control->set_name(name);
    }
    parent->add_child(control);
    return control;
}

// Give each compatibility section the same work surface as the current map editor.
VBoxContainer *pane(Node *parent, const String &heading, const String &help, bool expand_width = false) {
    auto *background = ui::surface_panel(parent, ui::Surface::HEADER);
    if (expand_width) {
        background->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    }
    auto *margin = add<MarginContainer>(background);
    for (const char *edge : {"margin_left", "margin_right", "margin_top", "margin_bottom"}) {
        margin->add_theme_constant_override(edge, ui::pixels(background, spacing::lg));
    }
    auto *content = add<VBoxContainer>(margin);
    content->add_theme_constant_override("separation", ui::pixels(content, spacing::md));
    auto *title = ui::label(content, heading, ui::LabelStyle::HEADING);
    ui::tooltip(title, help);
    return content;
}

// Enter one property on a single line. Apply when the user confirms or leaves the field.
LineEdit *text_field(Node *parent, const String &title, const String &value, const String &help) {
    auto *field = ui::text_input(nullptr, value, help);
    ui::field(parent, title, field, help);
    return field;
}

// Find one authored action by its stable ID.
Ref<InputAction> find_action(const Ref<InputLayout> &layout, const StringName &id) {
    if (layout.is_valid()) {
        for (const Variant &value : layout->get_actions()) {
            const Ref<InputAction> entry = value;
            if (entry.is_valid() && entry->get_id() == id) {
                return entry;
            }
        }
    }
    return {};
}

// Include the default map and retain the first appearance order of all other maps.
PackedStringArray map_names(const Ref<InputLayout> &layout, const StringName &pending_map) {
    PackedStringArray names{String(InputAction::DEFAULT_MAP)};
    for (const Variant &value : layout->get_actions()) {
        const Ref<InputAction> entry = value;
        if (entry.is_valid() && !names.has(String(entry->get_map()))) {
            names.append(String(entry->get_map()));
        }
    }
    if (!pending_map.is_empty() && !names.has(String(pending_map))) {
        names.append(String(pending_map));
    }
    return names;
}

// Fork a resource without sharing mutable properties with the source after edits.
template <class T>
Ref<T> fork(const Ref<T> &resource) {
    return resource->duplicate();
}

// Isolate from Inspector edits to nested or external resources outside this window.
Ref<InputLayout> clone_layout(const Ref<InputLayout> &resource) {
    return resource->duplicate_deep(Resource::DEEP_DUPLICATE_ALL);
}

// Check content rather than resource identity so external Inspector edits cannot be overwritten.
PackedByteArray layout_revision(const Ref<InputLayout> &layout) {
    return layout.is_valid() ? UtilityFunctions::var_to_bytes_with_objects(layout) : PackedByteArray();
}

// Group common device types, keeping empty slots and authored ordering within each group.
int device_group(const Ref<InputEvent> &event) {
    if (event.is_null()) {
        return 3;
    }
    if (is_gamepad(event)) {
        return 2;
    }
    return Object::cast_to<InputEventKey>(event.ptr()) ? 0 : 1;
}

// Provide a stable binding ID even after the physical control or action name changes.
Ref<InputBinding> make_binding(const StringName &action_id, const String &name,
        const Ref<InputEvent> &event, Vector3 contribution) {
    Ref<InputBinding> binding;
    binding.instantiate();
    binding->set_id(StringName(String(action_id) + "/" + Resource::generate_scene_unique_id()));
    binding->set_display_name(name);
    binding->set_event(event);
    binding->set_contribution(contribution);
    return binding;
}

// Build one signed axis source from a stick.
Ref<InputEvent> stick_axis(JoyAxis axis, float sign) {
    Ref<InputEventJoypadMotion> event;
    event.instantiate();
    event->set_device(-1);
    event->set_axis(axis);
    event->set_axis_value(sign);
    return event;
}

// Normalize a captured raw event into an authored default.
Ref<InputEvent> capture_default(const Ref<InputEvent> &event, InputAction::ValueKind kind) {
    if (const Ref<InputEventJoypadMotion> axis = event; axis.is_valid()) {
        const float value = axis->get_axis_value();
        return std::isfinite(value) && std::abs(value) >= 0.65f && kind != InputAction::MOTION ?
                stick_axis(axis->get_axis(), value < 0.0f ? -1.0f : 1.0f) : Ref<InputEvent>();
    }
    if (!event->is_pressed() || event->is_echo() || event->is_canceled() ||
            kind == InputAction::MOTION || !valid_binding_event(event, kind)) {
        return {};
    }
    if (const Ref<InputEventKey> key = event; key.is_valid()) {
        Ref<InputEventKey> saved;
        saved.instantiate();
        if (key->get_physical_keycode() != KEY_NONE) {
            saved->set_physical_keycode(key->get_physical_keycode());
        } else {
            saved->set_keycode(key->get_keycode());
        }
        saved->set_alt_pressed(key->is_alt_pressed() && key->get_keycode() != KEY_ALT);
        saved->set_ctrl_pressed(key->is_ctrl_pressed() && key->get_keycode() != KEY_CTRL);
        saved->set_shift_pressed(key->is_shift_pressed() && key->get_keycode() != KEY_SHIFT);
        saved->set_meta_pressed(key->is_meta_pressed() && key->get_keycode() != KEY_META);
        return saved;
    }
    if (const Ref<InputEventMouseButton> click = event; click.is_valid()) {
        Ref<InputEventMouseButton> saved;
        saved.instantiate();
        saved->set_button_index(click->get_button_index());
        saved->set_alt_pressed(click->is_alt_pressed());
        saved->set_ctrl_pressed(click->is_ctrl_pressed());
        saved->set_shift_pressed(click->is_shift_pressed());
        saved->set_meta_pressed(click->is_meta_pressed());
        return saved;
    }
    if (const Ref<InputEventJoypadButton> pad = event; pad.is_valid()) {
        Ref<InputEventJoypadButton> saved;
        saved.instantiate();
        saved->set_device(-1);
        saved->set_button_index(pad->get_button_index());
        return saved;
    }
    return {};
}

} // namespace

void InputActionsWindow::_bind_methods() {}

void InputActionsWindow::build() {
    const auto *editor = EditorInterface::get_singleton();
    set_name("CrimsonInputActions");
    set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    set_title("Input Actions");
    set_visible(false);
    set_transient(true);
    set_force_native(editor->is_multi_window_enabled());
    ui::embed_editor_popups(this);
    set_theme(ui::create_theme(this, editor->get_editor_scale()));
    set_min_size(Vector2i(ui::pixels(this, 700), ui::pixels(this, 430)));
    set_size(Vector2i(ui::pixels(this, 1040), ui::pixels(this, 690)));
    connect("close_requested", callable_mp(this, &InputActionsWindow::close_window));
    connect("focus_exited", callable_mp(this, &InputActionsWindow::cancel_listening));
    auto *shell = ui::surface_panel(this, ui::Surface::CANVAS);
    shell->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    auto *frame = add<MarginContainer>(shell);
    for (const char *edge : {"margin_left", "margin_right", "margin_top", "margin_bottom"}) {
        frame->add_theme_constant_override(edge, ui::pixels(this, spacing::lg));
    }
    auto *root = add<VBoxContainer>(frame);
    root->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *toolbar = add<HBoxContainer>(root);
    ui::button(toolbar, "New Layout", "Start a new input layout.", callable_mp(this, &InputActionsWindow::new_layout));
    ui::button(toolbar, "Open Layout", "Open an existing InputLayout resource.", callable_mp(this, &InputActionsWindow::open_layout));
    save_button = ui::button(toolbar, "Save", "Apply and save the layout (Ctrl+S).", callable_mp(this, &InputActionsWindow::save),
            ui::ButtonStyle::PRIMARY);
    undo_button = ui::button(toolbar, "Undo", "Undo the latest edit in this draft (Ctrl+Z).", callable_mp(this, &InputActionsWindow::undo));
    redo_button = ui::button(toolbar, "Redo", "Redo the latest edit in this draft (Ctrl+Y).", callable_mp(this, &InputActionsWindow::redo));
    path_label = ui::label(toolbar, String(), ui::LabelStyle::MUTED);
    path_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    path_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    path_label->set_clip_text(true);
    path_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);

    auto *columns = add<HBoxContainer>(root);
    columns->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    columns->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *map_pane = pane(columns, "Action Maps", "Group actions by what the player is doing, such as Gameplay, Menus, or Vehicle.");
    maps = add<ItemList>(map_pane, "ActionMaps");
    maps->set_custom_minimum_size(Vector2(ui::pixels(this, 144), 0));
    maps->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    maps->connect("item_selected", callable_mp(this, &InputActionsWindow::select_map));
    ui::button(map_pane, "+ Add Map", "Create a group and add its first action.", callable_mp(this, &InputActionsWindow::prompt_map));
    auto *action_pane = pane(columns, "Actions", "Actions describe what players can do. Each action can have several device bindings.");
    search = ui::search_input(action_pane, "Search actions", "Filter this map by action name or ID.");
    search->set_name("ActionSearch");
    search->connect("text_changed", callable_mp(this, &InputActionsWindow::filter_actions));
    actions = add<ItemList>(action_pane, "Actions");
    actions->set_custom_minimum_size(Vector2(ui::pixels(this, 172), 0));
    actions->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    actions->connect("item_selected", callable_mp(this, &InputActionsWindow::select_action));
    ui::button(action_pane, "+ Add Action", "Name an action and choose its value type.", callable_mp(this, &InputActionsWindow::prompt_action));
    ui::button(action_pane, "Add FPS Defaults", "Add missing Move, Jump, Mouse Aim, and Stick Aim in one edit.",
            callable_mp(this, &InputActionsWindow::add_player_defaults));
    ui::button(action_pane, "Add Settings Shortcut", "Add an action bound to Escape and gamepad Start.",
            callable_mp(this, &InputActionsWindow::add_settings_shortcut));

    auto *right = pane(columns, "Action Details", "Change the selected action and its device bindings.", true);
    auto *scroll = add<ScrollContainer>(right);
    scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    scroll->set_follow_focus(true);
    scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    details = add<VBoxContainer>(scroll, "ActionDetails");
    details->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    details->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    status = ui::label(root, String(), ui::LabelStyle::MUTED);
    status->set_name("InputStatus");
    status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);

    file_dialog = add<EditorFileDialog>(this);
    ui::style_file_dialog(file_dialog);
    file_dialog->set_access(FileDialog::ACCESS_RESOURCES);
    file_dialog->add_filter("*.tres,*.res", "Godot resources");
    file_dialog->connect("file_selected", callable_mp(this, &InputActionsWindow::file_selected));

    discard_dialog = ui::editor_modal(this, shell, callable_mp(this, &InputActionsWindow::confirm_discard),
            callable_mp(this, &InputActionsWindow::cancel_discard));
    discard_dialog->set_name("DiscardLayoutDraft");
    discard_dialog->set_confirm_text("Discard and Continue");
    discard_dialog->set_destructive(true);

    remove_dialog = ui::removal_confirmation(this, shell, callable_mp(this, &InputActionsWindow::confirm_remove),
            callable_mp(this, &InputActionsWindow::cancel_remove));

    map_dialog = ui::editor_modal(this, shell, callable_mp(this, &InputActionsWindow::add_map));
    map_dialog->set_name("AddActionMap");
    map_dialog->set_confirm_text("Continue");
    map_dialog->set_validator(callable_mp(this, &InputActionsWindow::validate_map_name));
    auto *map_fields = map_dialog->body();
    map_name = text_field(map_fields, "Map name", String(), "The name used to enable or disable this map for a local player.");
    map_name->set_placeholder("e.g. Menus");
    map_dialog->register_text_enter(map_name);

    action_dialog = ui::editor_modal(this, shell, callable_mp(this, &InputActionsWindow::add_action));
    action_dialog->set_name("AddLayoutAction");
    action_dialog->set_confirm_text("Create Action");
    action_dialog->set_validator(callable_mp(this, &InputActionsWindow::validate_action_name));
    auto *action_fields = action_dialog->body();
    action_name = text_field(action_fields, "Action name", String(), "The name appears in the list and supplies an initial script ID.");
    action_name->set_placeholder("e.g. Interact");
    action_kind = ui::select(nullptr, {kind_names[0], kind_names[1], kind_names[2], kind_names[3]}, 0,
            {}, ui::FieldState::EDITABLE, ui::SelectStyle::MENU);
    ui::field(action_fields, "Value type", action_kind, "The value that scripts can read from this action.");
    action_dialog->register_text_enter(action_name);

    control_dialog = ui::editor_modal(this, shell, callable_mp(this, &InputActionsWindow::apply_control_choice));
    control_dialog->set_name("ChooseLayoutBinding");
    control_dialog->set_confirm_text("Use Control");
    control_dialog->set_validator(callable_mp(this, &InputActionsWindow::validate_control_choice));
    auto *binding_fields = control_dialog->body();
    control_device = ui::select(binding_fields, {"Keyboard", "Mouse", "Gamepad Button", "Gamepad Axis"}, KEYBOARD,
            callable_mp(this, &InputActionsWindow::select_control_device));
    ui::tooltip(control_device->get_trigger(), "Choose which device supplies the physical input.");
    control_key = ui::text_input(binding_fields, {}, "Enter a Godot key name to bind its physical position.",
            ui::FieldState::EDITABLE, "Key name, e.g. Escape, Space, F, or F1");
    control_dialog->register_text_enter(control_key);
    control_choice = ui::select(binding_fields, {});
    ui::tooltip(control_choice->get_trigger(), "Choose the device control or signed axis direction to bind.");
}

void InputActionsWindow::open(const Ref<InputLayout> &layout, Object *owner, bool resume) {
    const ObjectID owner_id(owner ? owner->get_instance_id() : 0);
    if (!resume || draft.is_null()) {
        const bool changed_outside = layout.is_valid() && layout == source && layout_revision(layout) != source_revision;
        const bool switching = layout != source || owner_id != assignment_owner || changed_outside;
        if (switching) {
            if (auto *modal = active_modal(); modal && modal != discard_dialog) {
                modal->cancel();
            }
        }
        if (draft.is_valid() && draft != last_saved && switching) {
            next_source = layout;
            next_owner = owner_id;
            show();
            discard_dialog->open("Discard layout draft?", "This layout has unsaved edits.");
            return;
        }
        if (draft.is_null() || layout != source || owner_id != assignment_owner || draft == last_saved) {
            load_draft(layout, owner_id);
        }
    }
    if (is_visible()) {
        grab_focus();
    } else {
        popup_centered_clamped(get_size(), 0.88f);
    }
    actions->grab_focus();
}

void InputActionsWindow::load_draft(const Ref<InputLayout> &layout, ObjectID owner) {
    replacing_draft = true;
    cancel_listening();
    source = layout;
    assignment_owner = owner;
    const auto *target = ObjectDB::get_instance(owner);
    original_assignment = target ? Ref<InputLayout>(target->get("input_layout")) : Ref<InputLayout>();
    draft = layout.is_valid() ? clone_layout(layout) : InputLayout::create();
    source_revision = layout_revision(source);
    last_saved = draft;
    history = {draft};
    revision = 0;
    selected_map = InputAction::DEFAULT_MAP;
    selected_action = StringName();
    pending_map = StringName();
    search->clear();
    redraw();
    replacing_draft = false;
}

Ref<InputLayout> InputActionsWindow::replace_action(const Ref<InputAction> &entry) const {
    const Ref<InputLayout> next = fork(draft);
    TypedArray<InputAction> entries = next->get_actions();
    const Ref<InputAction> previous = current_action();
    for (int64_t i = 0; i < entries.size(); ++i) {
        if (Ref<InputAction>(entries[i]) == previous) {
            entries[i] = entry;
            next->set_actions(entries);
            return next;
        }
    }
    return {};
}

Ref<InputAction> InputActionsWindow::current_action() const {
    return find_action(draft, selected_action);
}

void InputActionsWindow::stage(const Ref<InputLayout> &next) {
    if (next.is_null()) {
        return;
    }
    history.resize(revision + 1);
    history.push_back(next);
    constexpr size_t history_limit = 100;
    if (history.size() > history_limit) {
        history.erase(history.begin());
    }
    revision = history.size() - 1;
    draft = next;
    queue_redraw();
}

void InputActionsWindow::queue_redraw() {
    if (!redraw_pending) {
        redraw_pending = true;
        callable_mp(this, &InputActionsWindow::redraw).call_deferred();
    }
}

void InputActionsWindow::redraw() {
    redraw_pending = false;
    if (draft.is_null()) {
        return;
    }
    const PackedStringArray names = map_names(draft, pending_map);
    const auto entries = draft->get_actions();
    if (!names.has(String(selected_map))) {
        selected_map = InputAction::DEFAULT_MAP;
    }
    maps->clear();
    for (const String &name : names) {
        int count = 0;
        for (const Variant &value : entries) {
            const Ref<InputAction> entry = value;
            count += entry.is_valid() && entry->get_map() == StringName(name);
        }
        const int index = maps->add_item(name);
        maps->set_item_metadata(index, StringName(name));
        maps->set_item_tooltip(index, name + String("\n") + String::num_int64(count) +
                String(" actions. Enable or disable this map on a local Controls instance."));
        if (String(selected_map) == name) {
            maps->select(index);
        }
    }
    filter_actions(search->get_text());
    refresh_toolbar();
    show_validation();
}

void InputActionsWindow::filter_actions(const String &query) {
    actions->clear();
    int selection = -1;
    const String needle = query.strip_edges().to_lower();
    for (const Variant &value : draft->get_actions()) {
        const Ref<InputAction> entry = value;
        if (entry.is_null() || entry->get_map() != selected_map) {
            continue;
        }
        const String id = entry->get_id();
        const String display = entry->get_display_name().is_empty() ? id.capitalize() : entry->get_display_name();
        if (!needle.is_empty() && !display.to_lower().contains(needle) && !id.to_lower().contains(needle)) {
            continue;
        }
        const int index = actions->add_item(display);
        actions->set_item_metadata(index, entry->get_id());
        const int kind = std::clamp<int>(entry->get_kind(), 0, InputAction::MOTION);
        actions->set_item_tooltip(index, id + String("\n") + kind_names[kind]);
        if (entry->get_id() == selected_action) {
            selection = index;
        }
    }
    if (selection < 0 && actions->get_item_count() != 0) {
        selection = 0;
    }
    selected_action = selection < 0 ? StringName() : StringName(actions->get_item_metadata(selection));
    if (selection >= 0) {
        actions->select(selection);
        actions->ensure_current_is_visible();
    }
    build_details(current_action());
}

void InputActionsWindow::select_map(int64_t index) {
    cancel_listening();
    selected_map = maps->get_item_metadata(index);
    selected_action = StringName();
    search->clear();
    filter_actions(String());
}

void InputActionsWindow::select_action(int64_t index) {
    cancel_listening();
    selected_action = actions->get_item_metadata(index);
    build_details(current_action());
}

void InputActionsWindow::refresh_toolbar() {
    String name = source.is_valid() ? source->get_path() : String();
    if (name.is_empty()) {
        name = "Untitled Input Layout";
    }
    path_label->set_text(name + (draft != last_saved ? "  *" : ""));
    path_label->set_tooltip_text(name);
    save_button->set_disabled(draft.is_null());
    undo_button->set_disabled(revision == 0);
    redo_button->set_disabled(revision + 1 >= history.size());
}

void InputActionsWindow::show_validation() {
    if (!listening_slot.is_empty()) {
        return;
    }
    switch (draft->validate()) {
        case OK:
            status->set_text("Gameplay starts enabled. Your game can enable other maps for each local player.");
            break;
        case ERR_ALREADY_EXISTS:
            status->set_text("Duplicate action or binding IDs. Each ID must be unique within the layout.");
            break;
        case ERR_DOES_NOT_EXIST:
            status->set_text("Choose a valid Button action to release the pointer, or select None.");
            break;
        default:
            status->set_text("Check for an empty name, an invalid binding, or a dead zone outside the supported range.");
            break;
    }
}

void InputActionsWindow::build_details(const Ref<InputAction> &entry) {
    for (int64_t i = details->get_child_count() - 1; i >= 0; --i) {
        Node *child = details->get_child(i);
        details->remove_child(child);
        child->queue_free();
    }
    if (entry.is_null()) {
        auto *message = ui::label(details, search->get_text().is_empty() ?
                "This map is empty. Add an action to give the player something to do." : "No actions match this search.");
        message->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    } else {
        const StringName id = entry->get_id();
        ui::label(details, kind_names[std::clamp<int>(entry->get_kind(), 0, InputAction::MOTION)], ui::LabelStyle::HEADING);
        for (const char *key : {"display_name", "id"}) {
            const bool is_id = StringName(key) == StringName("id");
            auto *field = text_field(details, is_id ? "Action ID" : "Name", is_id ? String(id) : entry->get_display_name(),
                    is_id ? "Scripts use this ID. Update any script or player role that references it if you rename it." :
                            "Friendly label shown in the action list.");
            field->connect("text_submitted", callable_mp(this, &InputActionsWindow::edit_action).bind(id, StringName(key)));
            field->connect("focus_exited", callable_mp(this, &InputActionsWindow::finish_action_field).bind(field, id, StringName(key)));
        }
        const auto maps_for_action = map_names(draft, pending_map);
        auto *map_picker = ui::select(nullptr, maps_for_action, maps_for_action.find(String(entry->get_map())),
                callable_mp(this, &InputActionsWindow::move_action).bind(maps_for_action));
        ui::field(details, "Map", map_picker, "Move this action to another action map without changing its ID or bindings.");
        if (entry->get_kind() == InputAction::AXIS || entry->get_kind() == InputAction::VECTOR) {
            auto *field = text_field(details, "Dead zone", String::num(entry->get_deadzone()),
                    "Ignore analog input near the centre. Enter a number from 0 up to, but below, 1.");
            field->connect("text_submitted", callable_mp(this, &InputActionsWindow::edit_deadzone).bind(id));
            field->connect("focus_exited", callable_mp(this, &InputActionsWindow::finish_deadzone).bind(field, id));
        }
        auto *requires_pointer = ui::checkbox(details, "Only while mouse is captured",
                entry->get_requires_pointer() ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED,
                callable_mp(this, &InputActionsWindow::edit_pointer_requirement));
        ui::tooltip(requires_pointer, "This action is inactive while the mouse pointer is free.");

        auto *binding_toolbar = add<HBoxContainer>(details);
        auto *heading = ui::label(binding_toolbar, "Bindings", ui::LabelStyle::HEADING);
        heading->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        ui::button(binding_toolbar, "+ Add Binding", "Create a new binding, then press Listen to choose its physical control.",
                callable_mp(this, &InputActionsWindow::add_binding).bind(EMPTY));
        if (entry->get_kind() == InputAction::VECTOR) {
            auto *presets = ui::select(binding_toolbar, {"WASD", "Left Stick", "Right Stick"}, -1,
                    callable_mp(this, &InputActionsWindow::add_binding_preset));
            presets->set_placeholder("Add Preset...");
            presets->set_h_size_flags(Control::SIZE_SHRINK_END);
            presets->set_custom_minimum_size(Vector2(ui::pixels(this, 112), ui::pixels(this, 24)));
            ui::tooltip(presets->get_trigger(), "Add a complete directional binding preset.");
        }
        constexpr const char *groups[] = {"Keyboard", "Mouse", "Gamepad", "Unassigned"};
        for (int group = 0; group < 4; ++group) {
            VBoxContainer *binding_group = nullptr;
            for (const Variant &value : entry->get_bindings()) {
                const Ref<InputBinding> binding = value;
                if (binding.is_valid() && device_group(binding->get_event()) == group) {
                    if (!binding_group) {
                        binding_group = pane(details, groups[group], "Bindings on this device contribute to the selected action.");
                    }
                    build_binding(binding_group, binding, entry->get_kind());
                }
            }
        }
        ui::button(details, "Remove Action", "Remove the selected action and its bindings from this draft.",
                callable_mp(this, &InputActionsWindow::prompt_remove_action));
    }
    auto *settings = pane(details, "Layout Pointer", "Default pointer behavior shared by all maps in this layout.");
    auto *click = ui::checkbox(settings, "Click in game to capture mouse",
            draft->get_click_to_capture() ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED,
            callable_mp(this, &InputActionsWindow::edit_click_capture));
    ui::tooltip(click, "A fresh unhandled left click captures the pointer during gameplay.");
    PackedStringArray release_names{"No pointer release action"};
    PackedStringArray release_ids{String()};
    for (const Variant &value : draft->get_actions()) {
        const Ref<InputAction> candidate = value;
        if (candidate.is_valid() && candidate->get_kind() == InputAction::BUTTON) {
            release_ids.append(String(candidate->get_id()));
            release_names.append(String("Release mouse with ") + String(candidate->get_id()));
        }
    }
    const int release_selected = std::max<int>(0, release_ids.find(String(draft->get_release_action())));
    auto *release = ui::select(settings, release_names, release_selected,
            callable_mp(this, &InputActionsWindow::edit_release_action).bind(release_ids));
    ui::tooltip(release->get_trigger(), "Choose a Button action that releases the captured mouse, or leave it off.");
}

void InputActionsWindow::build_binding(VBoxContainer *panel, const Ref<InputBinding> &binding, InputAction::ValueKind kind) {
    auto *row = add<HBoxContainer>(panel);
    const StringName slot = binding->get_id();
    const Ref<InputEvent> event = binding->get_event();
    const String source = editor_labels::control_name(event);
    auto *binding_label = ui::label(row, source);
    binding_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    binding_label->set_clip_text(true);
    binding_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    binding_label->set_tooltip_text(source + String("\nBinding ID: ") + String(slot) +
            (binding->get_display_name().is_empty() ? String() : String("\n") + binding->get_display_name()));
    if (kind == InputAction::AXIS || kind == InputAction::VECTOR) {
        PackedStringArray labels;
        PackedVector3Array contributions;
        if (kind == InputAction::AXIS) {
            labels = {"Positive", "Negative"};
            contributions = {Vector3(1, 0, 0), Vector3(-1, 0, 0)};
        } else {
            labels = {"Left", "Right", "Up", "Down"};
            contributions = {Vector3(-1, 0, 0), Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(0, 1, 0)};
        }
        const Vector3 authored = binding->get_contribution();
        int selected = contributions.find(authored);
        if (selected == -1) {
            selected = labels.size();
            labels.append("Custom");
            contributions.append(authored);
        }
        auto *direction = ui::select(row, labels, selected, callable_mp(this, &InputActionsWindow::set_direction).bind(slot, contributions));
        direction->set_custom_minimum_size(Vector2(ui::pixels(this, 80), ui::pixels(this, 24)));
        direction->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
        ui::tooltip(direction->get_trigger(), "Choose how this control contributes to the action value.");
    }
    if (kind != InputAction::MOTION) {
        ui::button(row, "Choose...", "Pick a key, mouse button, or gamepad control manually.",
                callable_mp(this, &InputActionsWindow::choose_binding).bind(slot));
        auto *listen = ui::button(row, "Listen", "Press a key, mouse button, or controller control. Escape cancels.");
        listen->connect("pressed", callable_mp(this, &InputActionsWindow::start_listening).bind(slot, listen));
    }
    ui::button(row, "Remove", "Remove this binding slot and its saved ID.",
            callable_mp(this, &InputActionsWindow::prompt_remove_binding).bind(slot));
}

void InputActionsWindow::edit_action(const String &value, StringName id, StringName property) {
    const auto entry = current_action();
    if (entry.is_null() || entry->get_id() != id) {
        return;
    }
    const String trimmed = value.strip_edges();
    if (String(entry->get(property)) == trimmed) {
        return;
    }
    if (property == StringName("id") && (trimmed.is_empty() || find_action(draft, StringName(trimmed)).is_valid())) {
        status->set_text("Enter a unique, non-empty action ID.");
        return;
    }
    const auto next_entry = fork(entry);
    next_entry->set(property, property == StringName("id") ? Variant(StringName(trimmed)) : Variant(trimmed));
    const auto next = replace_action(next_entry);
    if (property == StringName("id")) {
        selected_action = next_entry->get_id();
        if (draft->get_release_action() == id) {
            next->set_release_action(selected_action);
        }
    }
    stage(next);
}

void InputActionsWindow::finish_action_field(LineEdit *field, StringName id, StringName property) {
    if (!replacing_draft && field->is_inside_tree() && !redraw_pending) {
        edit_action(field->get_text(), id, property);
    }
}

void InputActionsWindow::finish_deadzone(LineEdit *field, StringName id) {
    if (!replacing_draft && field->is_inside_tree() && !redraw_pending) {
        edit_deadzone(field->get_text(), id);
    }
}

void InputActionsWindow::edit_deadzone(const String &value, StringName id) {
    const auto entry = current_action();
    if (entry.is_null() || entry->get_id() != id) {
        return;
    }
    const String text = value.strip_edges();
    const double deadzone = text.to_float();
    if (!text.is_valid_float() || !std::isfinite(deadzone) || deadzone < 0.0 || deadzone >= 1.0) {
        status->set_text("Dead zone must be a number from 0 up to, but below, 1.");
        return;
    }
    if (deadzone != entry->get_deadzone()) {
        const auto next_entry = fork(entry);
        next_entry->set_deadzone(deadzone);
        stage(replace_action(next_entry));
    }
}

void InputActionsWindow::edit_pointer_requirement(bool value) {
    if (const auto entry = current_action(); entry.is_valid()) {
        const auto next_entry = fork(entry);
        next_entry->set_requires_pointer(value);
        stage(replace_action(next_entry));
    }
}

void InputActionsWindow::move_action(int64_t index, PackedStringArray names) {
    if (const auto entry = current_action(); entry.is_valid()) {
        const auto next_entry = fork(entry);
        next_entry->set_map(StringName(names[index]));
        selected_map = next_entry->get_map();
        if (selected_map == pending_map) {
            pending_map = StringName();
        }
        stage(replace_action(next_entry));
    }
}

void InputActionsWindow::edit_click_capture(bool value) {
    const auto next = fork(draft);
    next->set_click_to_capture(value);
    stage(next);
}

void InputActionsWindow::edit_release_action(int64_t index, PackedStringArray action_ids) {
    const auto next = fork(draft);
    next->set_release_action(StringName(action_ids[index]));
    stage(next);
}

void InputActionsWindow::prompt_map() {
    map_name->clear();
    map_dialog->open("Add Action Map", {}, map_name);
}

void InputActionsWindow::add_map() {
    const String name = map_name->get_text().strip_edges();
    pending_map = selected_map = StringName(name);
    selected_action = StringName();
    search->clear();
    redraw();
    callable_mp(this, &InputActionsWindow::prompt_action).call_deferred();
}

bool InputActionsWindow::validate_map_name() {
    const String name = map_name->get_text().strip_edges();
    const bool valid = !name.is_empty() && !map_names(draft, pending_map).has(name);
    map_dialog->set_validation_message(valid ? String() : String("Enter a new, non-empty map name."));
    if (!valid) {
        map_name->grab_focus();
    }
    return valid;
}

void InputActionsWindow::prompt_action() {
    action_name->clear();
    action_kind->select(0);
    action_dialog->open("Add Action", {}, action_name);
}

void InputActionsWindow::add_action() {
    const String name = action_name->get_text().strip_edges();
    String base = name.to_snake_case().validate_node_name().replace(" ", "_");
    if (base.is_empty()) {
        base = "action";
    }
    StringName id(base);
    int suffix = 2;
    while (find_action(draft, id).is_valid()) {
        id = StringName(base + "_" + String::num_int64(suffix++));
    }
    Ref<InputAction> entry;
    entry.instantiate();
    entry->set_id(id);
    entry->set_display_name(name);
    entry->set_map(selected_map);
    entry->set_kind(static_cast<InputAction::ValueKind>(action_kind->get_selected_index()));
    if (entry->get_kind() == InputAction::MOTION) {
        Ref<InputEventMouseMotion> motion;
        motion.instantiate();
        entry->set_bindings({make_binding(id, "Mouse Motion", motion, Vector3(1, 1, 0))});
        entry->set_requires_pointer(true);
    }
    const auto next = fork(draft);
    auto entries = next->get_actions();
    entries.append(entry);
    next->set_actions(entries);
    if (pending_map == selected_map) {
        pending_map = StringName();
    }
    selected_action = id;
    search->clear();
    stage(next);
}

bool InputActionsWindow::validate_action_name() {
    const bool valid = !action_name->get_text().strip_edges().is_empty();
    action_dialog->set_validation_message(valid ? String() : String("Enter an action name before creating it."));
    if (!valid) {
        action_name->grab_focus();
    }
    return valid;
}

void InputActionsWindow::remove_action() {
    const auto next = fork(draft);
    auto entries = next->get_actions();
    entries.erase(current_action());
    if (next->get_release_action() == selected_action) {
        next->set_release_action(StringName());
    }
    next->set_actions(entries);
    selected_action = StringName();
    stage(next);
}

void InputActionsWindow::prompt_remove_action() {
    const auto entry = current_action();
    if (entry.is_null()) {
        return;
    }
    cancel_listening();
    removal_action = entry->get_id();
    removal_slot = StringName();
    const String name = entry->get_display_name().is_empty() ? String(removal_action) : entry->get_display_name();
    remove_dialog->open(String("Remove \"") + name + "\"?", "This action and its bindings will be removed from the layout.");
}

void InputActionsWindow::prompt_remove_binding(StringName slot) {
    const auto entry = current_action();
    if (entry.is_null()) {
        return;
    }
    for (const Variant &value : entry->get_bindings()) {
        const Ref<InputBinding> binding = value;
        if (binding.is_valid() && binding->get_id() == slot) {
            cancel_listening();
            removal_action = entry->get_id();
            removal_slot = slot;
            const String name = editor_labels::control_name(binding->get_event());
            remove_dialog->open(String("Remove \"") + name + "\"?", "This binding will be removed from the layout.");
            return;
        }
    }
}

void InputActionsWindow::confirm_remove() {
    if (!removal_action.is_empty() && selected_action == removal_action) {
        if (removal_slot.is_empty()) {
            remove_action();
        } else {
            remove_binding(removal_slot);
        }
    }
    cancel_remove();
}

void InputActionsWindow::cancel_remove() {
    removal_action = StringName();
    removal_slot = StringName();
}

void InputActionsWindow::add_player_defaults() {
    const auto next = fork(draft);
    const int64_t count = next->get_actions().size();
    if (find_action(next, "move").is_null()) {
        next->add_move("move");
        find_action(next, "move")->set_display_name("Move");
    }
    if (find_action(next, "jump").is_null()) {
        next->add_button("jump", KEY_SPACE);
        find_action(next, "jump")->set_display_name("Jump");
    }
    if (find_action(next, "mouse_aim").is_null()) {
        next->add_mouse_motion("mouse_aim");
        find_action(next, "mouse_aim")->set_display_name("Mouse Aim");
    }
    if (find_action(next, "stick_aim").is_null()) {
        next->add_right_stick("stick_aim");
        find_action(next, "stick_aim")->set_display_name("Stick Aim");
    }
    if (next->get_actions().size() == count) {
        status->set_text("The four default player actions are already present.");
        return;
    }
    selected_map = InputAction::DEFAULT_MAP;
    search->clear();
    stage(next);
}

void InputActionsWindow::add_settings_shortcut() {
    if (find_action(draft, "settings").is_valid()) {
        status->set_text("An action with the settings ID already exists.");
        return;
    }
    const auto next = fork(draft);
    next->add_button("settings", KEY_ESCAPE, JOY_BUTTON_START);
    find_action(next, "settings")->set_display_name("Open Settings");
    selected_map = InputAction::DEFAULT_MAP;
    selected_action = "settings";
    search->clear();
    stage(next);
}

void InputActionsWindow::add_binding(int64_t preset) {
    const auto entry = current_action();
    if (entry.is_null()) {
        return;
    }
    const auto next_entry = fork(entry);
    auto bindings = next_entry->get_bindings();
    const StringName id = next_entry->get_id();
    if (entry->get_kind() == InputAction::MOTION) {
        if (!bindings.is_empty()) {
            status->set_text("This action already has a Mouse Motion binding.");
            return;
        }
        Ref<InputEventMouseMotion> motion;
        motion.instantiate();
        bindings.append(make_binding(id, "Mouse Motion", motion, Vector3(1, 1, 0)));
    } else if (preset == EMPTY) {
        bindings.append(make_binding(id, String(), {}, Vector3(1, 0, 0)));
    } else {
        constexpr const char *directions[] = {"Left", "Right", "Up", "Down"};
        const Vector3 contributions[] = {Vector3(-1, 0, 0), Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(0, 1, 0)};
        const Key keys[] = {KEY_A, KEY_D, KEY_W, KEY_S};
        const bool right = preset == RIGHT_STICK;
        for (int i = 0; i < 4; ++i) {
            Ref<InputEvent> event;
            if (preset == WASD) {
                Ref<InputEventKey> key;
                key.instantiate();
                key->set_physical_keycode(keys[i]);
                event = key;
            } else {
                const JoyAxis axis = i < 2 ? (right ? JOY_AXIS_RIGHT_X : JOY_AXIS_LEFT_X) : (right ? JOY_AXIS_RIGHT_Y : JOY_AXIS_LEFT_Y);
                event = stick_axis(axis, i % 2 == 0 ? -1.0f : 1.0f);
            }
            bindings.append(make_binding(id, directions[i], event, contributions[i]));
        }
    }
    next_entry->set_bindings(bindings);
    stage(replace_action(next_entry));
}

void InputActionsWindow::set_direction(int64_t index, StringName slot, PackedVector3Array contributions) {
    const auto entry = current_action();
    if (entry.is_null()) {
        return;
    }
    const auto next_entry = fork(entry);
    auto bindings = next_entry->get_bindings();
    for (int64_t i = 0; i < bindings.size(); ++i) {
        const Ref<InputBinding> binding = bindings[i];
        if (binding.is_valid() && binding->get_id() == slot) {
            const auto next_binding = fork(binding);
            next_binding->set_contribution(contributions[index]);
            bindings[i] = next_binding;
            next_entry->set_bindings(bindings);
            stage(replace_action(next_entry));
            return;
        }
    }
}

void InputActionsWindow::add_binding_preset(int64_t index) {
    constexpr Preset commands[] = {WASD, LEFT_STICK, RIGHT_STICK};
    add_binding(commands[index]);
}

void InputActionsWindow::set_binding_event(const StringName &slot, const Ref<InputEvent> &event) {
    const auto entry = current_action();
    if (entry.is_null()) {
        return;
    }
    const auto next_entry = fork(entry);
    auto bindings = next_entry->get_bindings();
    for (int64_t i = 0; i < bindings.size(); ++i) {
        const Ref<InputBinding> binding = bindings[i];
        if (binding.is_valid() && binding->get_id() == slot) {
            const auto next_binding = fork(binding);
            next_binding->set_event(event);
            bindings[i] = next_binding;
            next_entry->set_bindings(bindings);
            stage(replace_action(next_entry));
            return;
        }
    }
}

void InputActionsWindow::choose_binding(StringName slot) {
    cancel_listening();
    manual_slot = slot;
    control_key->clear();
    control_device->select(KEYBOARD);
    select_control_device(KEYBOARD);
    control_dialog->open("Choose Binding", {}, control_key);
}

void InputActionsWindow::select_control_device(int64_t index) {
    control_key->set_visible(index == KEYBOARD);
    control_choice->set_visible(index != KEYBOARD);
    PackedStringArray captions;
    control_choice_ids.clear();
    if (index == MOUSE) {
        for (int button = MOUSE_BUTTON_LEFT; button <= MOUSE_BUTTON_XBUTTON2; ++button) {
            captions.append(editor_labels::mouse_button_name(static_cast<MouseButton>(button)));
            control_choice_ids.append(button);
        }
    } else if (index == PAD_BUTTON) {
        for (int button = JOY_BUTTON_A; button <= JOY_BUTTON_DPAD_RIGHT; ++button) {
            captions.append(editor_labels::gamepad_button_name(static_cast<JoyButton>(button)));
            control_choice_ids.append(button);
        }
    } else if (index == PAD_AXIS) {
        for (int axis = JOY_AXIS_LEFT_X; axis <= JOY_AXIS_TRIGGER_RIGHT; ++axis) {
            if (axis < JOY_AXIS_TRIGGER_LEFT) {
                captions.append(editor_labels::gamepad_axis_name(static_cast<JoyAxis>(axis), true));
                control_choice_ids.append(axis * 2);
            }
            captions.append(editor_labels::gamepad_axis_name(static_cast<JoyAxis>(axis), false));
            control_choice_ids.append(axis * 2 + 1);
        }
    }
    control_choice->set_items(captions, captions.is_empty() ? -1 : 0);
    control_dialog->set_validation_message({});
}

void InputActionsWindow::apply_control_choice() {
    Ref<InputEvent> event;
    const int device = control_device->get_selected_index();
    const int selected = control_choice->get_selected_index();
    if (device != KEYBOARD && (selected < 0 || selected >= control_choice_ids.size())) {
        return;
    }
    switch (device) {
        case KEYBOARD: {
            const Key value = OS::get_singleton()->find_keycode_from_string(control_key->get_text().strip_edges());
            Ref<InputEventKey> key;
            key.instantiate();
            key->set_physical_keycode(value);
            event = key;
            break;
        }
        case MOUSE: {
            Ref<InputEventMouseButton> click;
            click.instantiate();
            click->set_button_index(static_cast<MouseButton>(control_choice_ids[selected]));
            event = click;
            break;
        }
        case PAD_BUTTON: {
            Ref<InputEventJoypadButton> pad;
            pad.instantiate();
            pad->set_device(-1);
            pad->set_button_index(static_cast<JoyButton>(control_choice_ids[selected]));
            event = pad;
            break;
        }
        case PAD_AXIS: {
            const int id = control_choice_ids[selected];
            event = stick_axis(static_cast<JoyAxis>(id / 2), id % 2 == 0 ? -1.0f : 1.0f);
            break;
        }
    }
    if (event.is_valid()) {
        set_binding_event(manual_slot, event);
    }
}

bool InputActionsWindow::validate_control_choice() {
    if (control_device->get_selected_index() != KEYBOARD) {
        control_dialog->set_validation_message({});
        return true;
    }
    const Key value = OS::get_singleton()->find_keycode_from_string(control_key->get_text().strip_edges());
    const bool valid = value != KEY_NONE && value != KEY_UNKNOWN;
    control_dialog->set_validation_message(valid ? String() : String("Enter a known key, such as Escape or F1."));
    if (!valid) {
        control_key->grab_focus();
    }
    return valid;
}

void InputActionsWindow::remove_binding(StringName slot) {
    const auto entry = current_action();
    if (entry.is_null()) {
        return;
    }
    const auto next_entry = fork(entry);
    auto bindings = next_entry->get_bindings();
    for (int64_t i = 0; i < bindings.size(); ++i) {
        const Ref<InputBinding> binding = bindings[i];
        if (binding.is_valid() && binding->get_id() == slot) {
            bindings.remove_at(i);
            next_entry->set_bindings(bindings);
            stage(replace_action(next_entry));
            return;
        }
    }
}

void InputActionsWindow::start_listening(StringName slot, Button *control) {
    if (!listening_slot.is_empty()) {
        cancel_listening();
        return;
    }
    listening_slot = slot;
    listen_button = control->get_instance_id();
    control->set_text("Listening...");
    status->set_text("Press a key, click a mouse button, or move a controller control. Esc cancels.");
}

void InputActionsWindow::cancel_listening() {
    if (listening_slot.is_empty()) {
        return;
    }
    listening_slot = StringName();
    if (auto *control = Object::cast_to<Button>(ObjectDB::get_instance(listen_button)); control && control->is_inside_tree()) {
        control->set_text("Listen");
        if (has_focus()) {
            control->grab_focus();
        }
    }
    listen_button = ObjectID();
    show_validation();
}

void InputActionsWindow::_input(const Ref<InputEvent> &event) {
    if (!is_visible() || event.is_null() || active_modal()) {
        return;
    }
    const Ref<InputEventKey> key = event;
    if (!listening_slot.is_empty()) {
        if (key.is_valid() && key->is_pressed() && key->get_keycode() == KEY_ESCAPE) {
            set_input_as_handled();
            cancel_listening();
            return;
        }
        if (const auto entry = current_action(); entry.is_valid()) {
            const auto binding = capture_default(event, entry->get_kind());
            if (binding.is_valid()) {
                const StringName slot = listening_slot;
                set_input_as_handled();
                cancel_listening();
                set_binding_event(slot, binding);
                actions->grab_focus();
            }
        }
        return;
    }
    if (key.is_null() || !key->is_pressed() || key->is_echo() || (!key->is_ctrl_pressed() && !key->is_meta_pressed())) {
        return;
    }
    switch (key->get_keycode()) {
        case KEY_S:
            save();
            break;
        case KEY_Z:
            if (key->is_shift_pressed()) {
                redo();
            } else {
                undo();
            }
            break;
        case KEY_Y:
            redo();
            break;
        default:
            return;
    }
    set_input_as_handled();
}

void InputActionsWindow::undo() {
    cancel_listening();
    if (revision > 0) {
        draft = history[--revision];
        queue_redraw();
    }
}

void InputActionsWindow::redo() {
    cancel_listening();
    if (revision + 1 < history.size()) {
        draft = history[++revision];
        queue_redraw();
    }
}

void InputActionsWindow::new_layout() {
    if (draft != last_saved) {
        next_source.unref();
        next_owner = assignment_owner;
        discard_dialog->open("Discard layout draft?", "This layout has unsaved edits.");
    } else {
        load_draft({}, assignment_owner);
    }
}

void InputActionsWindow::open_layout() {
    opening_file = true;
    file_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
    file_dialog->popup_centered_clamped(Vector2i(670, 440));
}

void InputActionsWindow::save() {
    cancel_listening();
    if (draft->validate() != OK) {
        show_validation();
        return;
    }
    const String path = source.is_valid() ? source->get_path() : String();
    if (source.is_valid() && layout_revision(source) != source_revision) {
        status->set_text("This layout changed outside the window. Reopen it before applying this draft.");
        return;
    }
    if (path.is_empty()) {
        opening_file = false;
        file_dialog->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
        file_dialog->set_current_file("input_layout.tres");
        file_dialog->popup_centered_clamped(Vector2i(670, 440));
    } else if (path.contains("::")) {
        auto *editor = EditorInterface::get_singleton();
        const Node *root = editor->get_edited_scene_root();
        if (!root || path.get_slice("::", 0) != root->get_scene_file_path()) {
            status->set_text("Open the scene that owns this embedded layout before saving it.");
            return;
        }
        commit_source();
        const Error result = editor->save_scene();
        if (result == OK) {
            last_saved = draft;
            refresh_toolbar();
            status->set_text("Saved the layout with its scene.");
        } else {
            status->set_text("Godot applied the layout but could not save its scene: " + UtilityFunctions::error_string(result));
        }
    } else {
        save_external(path);
    }
}

void InputActionsWindow::file_selected(const String &path) {
    if (opening_file) {
        const Ref<InputLayout> loaded = ResourceLoader::get_singleton()->load(path, "InputLayout");
        if (loaded.is_null()) {
            status->set_text("Choose a saved InputLayout resource.");
            return;
        }
        open(loaded, ObjectDB::get_instance(assignment_owner));
        if (!discard_dialog->is_open()) {
            assign_source();
        }
    } else {
        const String destination = path.get_extension().is_empty() ? path + String(".tres") : path;
        if (ResourceLoader::get_singleton()->exists(destination)) {
            status->set_text("Choose a new file name. This layout already exists.");
            return;
        }
        save_external(destination);
    }
}

void InputActionsWindow::save_external(const String &path) {
    const auto output = clone_layout(draft);
    const Error result = ResourceSaver::get_singleton()->save(output, path);
    if (result != OK) {
        status->set_text("Could not save " + path.get_file() + ": " + UtilityFunctions::error_string(result));
        return;
    }
    if (source.is_valid() && source->get_path() == path) {
        commit_source();
    } else {
        source = output;
        source->take_over_path(path);
        assign_source();
    }
    last_saved = draft;
    source_revision = layout_revision(source);
    refresh_toolbar();
    status->set_text("Saved " + path.get_file());
}

void InputActionsWindow::commit_source() {
    if (draft == last_saved) {
        return;
    }
    const auto output = clone_layout(draft);
    auto *history_manager = EditorInterface::get_singleton()->get_editor_undo_redo();
    history_manager->create_action("Edit Input Actions", UndoRedo::MERGE_DISABLE, source.ptr());
    for (const char *property : {"actions", "click_to_capture", "release_action"}) {
        history_manager->add_do_property(source.ptr(), property, output->get(property));
        history_manager->add_undo_property(source.ptr(), property, source->get(property));
    }
    history_manager->commit_action();
    source_revision = layout_revision(source);
}

void InputActionsWindow::assign_source() {
    auto *owner = ObjectDB::get_instance(assignment_owner);
    if (!owner || source.is_null() || Ref<InputLayout>(owner->get("input_layout")) != original_assignment ||
            source == original_assignment) {
        return;
    }
    auto *history_manager = EditorInterface::get_singleton()->get_editor_undo_redo();
    history_manager->create_action("Assign Input Layout", UndoRedo::MERGE_DISABLE, owner);
    history_manager->add_do_property(owner, "input_layout", source);
    history_manager->add_undo_property(owner, "input_layout", original_assignment);
    history_manager->commit_action();
    original_assignment = source;
}

void InputActionsWindow::confirm_discard() {
    const auto next = next_source;
    const auto owner = next_owner;
    cancel_discard();
    load_draft(next, owner);
    assign_source();
    actions->grab_focus();
}

void InputActionsWindow::cancel_discard() {
    next_source.unref();
    next_owner = ObjectID();
}

void InputActionsWindow::close_window() {
    if (auto *modal = active_modal()) {
        modal->cancel();
        return;
    }
    cancel_listening();
    hide();
}

ui::EditorModal *InputActionsWindow::active_modal() const {
    for (auto *modal : {discard_dialog, remove_dialog, map_dialog, action_dialog, control_dialog}) {
        if (modal && modal->is_open()) {
            return modal;
        }
    }
    return nullptr;
}

} // namespace crimson::input

#endif
