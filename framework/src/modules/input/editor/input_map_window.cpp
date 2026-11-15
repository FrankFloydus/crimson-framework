// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "input_map_window.hpp"
#include "input_control_labels.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/input/input_sources.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/editor_file_dialog.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_file_system_directory.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/h_split_container.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_with_modifiers.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/menu_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_empty.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
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

// Resource path shipped by the addon and left untouched by the default save command.
constexpr const char *fps_preset = "res://addons/crimson-framework/input/fps.tres";

// Return the visible labels corresponding to the persisted input kind enum.
PackedStringArray value_kinds() { return {"Button", "1D Axis", "2D Vector", "Mouse Motion"}; }

// Read native action resource paths from the editor index, including classes derived from InputAction.
void collect_action_paths(EditorFileSystemDirectory *directory, PackedStringArray &paths) {
    if (!directory) {
        return;
    }
    for (int index = 0; index < directory->get_file_count(); ++index) {
        const StringName type = directory->get_file_type(index);
        if (type == StringName("InputAction") ||
                (!type.is_empty() && ClassDB::class_exists(type) && ClassDB::is_parent_class(type, "InputAction"))) {
            paths.append(directory->get_file_path(index));
        }
    }
    for (int index = 0; index < directory->get_subdir_count(); ++index) {
        collect_action_paths(directory->get_subdir(index), paths);
    }
}

// Stable IDs for commands in the footer's Map menu.
enum MapCommand { NEW_MAP, OPEN_MAP, OPEN_PRESET, MAP_SETTINGS, SAVE_COPY, UNDO_DRAFT, REDO_DRAFT };

// Godot's special key enum is sparse beyond F35. Pass only named values to the OS labeler.
constexpr Key extra_keys[] = {
    KEY_MENU, KEY_HYPER, KEY_HELP, KEY_BACK, KEY_FORWARD, KEY_STOP, KEY_REFRESH, KEY_VOLUMEDOWN,
    KEY_VOLUMEMUTE, KEY_VOLUMEUP, KEY_MEDIAPLAY, KEY_MEDIASTOP, KEY_MEDIAPREVIOUS, KEY_MEDIANEXT,
    KEY_MEDIARECORD, KEY_HOMEPAGE, KEY_FAVORITES, KEY_SEARCH, KEY_STANDBY, KEY_OPENURL, KEY_LAUNCHMAIL,
    KEY_LAUNCHMEDIA, KEY_LAUNCH0, KEY_LAUNCH1, KEY_LAUNCH2, KEY_LAUNCH3, KEY_LAUNCH4, KEY_LAUNCH5,
    KEY_LAUNCH6, KEY_LAUNCH7, KEY_LAUNCH8, KEY_LAUNCH9, KEY_LAUNCHA, KEY_LAUNCHB, KEY_LAUNCHC,
    KEY_LAUNCHD, KEY_LAUNCHE, KEY_LAUNCHF, KEY_GLOBE, KEY_KEYBOARD, KEY_JIS_EISU, KEY_JIS_KANA,
};

// Attach a child for the lifetime of its editor container.
template <class T>
T *add(Node *parent) {
    T *child = memnew(T);
    parent->add_child(child);
    return child;
}

// Duplicate map-owned resources without replacing a standalone action definition.
template <class T>
Ref<T> detached(const Ref<T> &resource) {
    return resource.is_valid() ? Ref<T>(resource->duplicate()) : Ref<T>();
}

// Track source edits by value so a draft cannot overwrite another inspector's edits.
PackedByteArray revision_of(const Ref<InputActionMap> &map) {
    return map.is_valid() ? UtilityFunctions::var_to_bytes_with_objects(map) : PackedByteArray();
}

// Give parent actions and leaf bindings separate tree identities.
StringName row_id(const StringName &id, bool binding) {
    return StringName(String(binding ? "binding/" : "action/") + String(id));
}

// Present the definition's name, with a readable fallback for empty resources.
String action_title(const Ref<InputActionMapping> &mapping) {
    const Ref<InputAction> action = mapping->get_action();
    if (action.is_valid() && !action->get_display_name().strip_edges().is_empty()) {
        return action->get_display_name();
    }
    return String(mapping->get_id()).replace("_", " ").capitalize();
}

// Present a pathless map and unnamed resources without a synthetic on-disk path.
String map_title(const Ref<InputActionMap> &map) {
    if (map.is_null()) {
        return "New Input Map";
    }
    const String name = map->get_display_name().strip_edges();
    return name.is_empty() ? String(map->get_id()) : name;
}

// Give a text-bearing control a stable width without expanding its entire pane.
void clip(Label *label) {
    label->set_clip_text(true);
    label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
}

// Attach scaled content insets.
MarginContainer *inset(Node *owner, int horizontal, int top, int bottom) {
    auto *margin = add<MarginContainer>(owner);
    const int side = ui::pixels(owner, horizontal);
    margin->add_theme_constant_override("margin_left", side);
    margin->add_theme_constant_override("margin_top", ui::pixels(owner, top));
    margin->add_theme_constant_override("margin_right", side);
    margin->add_theme_constant_override("margin_bottom", ui::pixels(owner, bottom));
    return margin;
}

// Divide the workbench only at structural boundaries.
void rule(Node *owner) {
    auto *separator = ui::surface_panel(owner, ui::Surface::RULE);
    separator->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    separator->set_custom_minimum_size(Vector2(0, ui::pixels(owner, 1)));
}

// Leave room for the field name when its value has only short choices.
void compact_field_value(Control *value, int width) {
    value->set_custom_minimum_size(Vector2(ui::pixels(value, width), 0));
    value->set_h_size_flags(Control::SIZE_SHRINK_END);
}

// Identify the icon representing an assigned physical source.
StringName device_icon(const Ref<InputBinding> &binding) {
    const auto event = binding->get_event();
    if (binding->get_stick() != InputBinding::NONE || is_gamepad(event)) {
        return "gamepad";
    }
    if (Ref<InputEventKey>(event).is_valid()) {
        return "keyboard";
    }
    return event.is_valid() ? StringName("mouse") : StringName("add");
}

// Use the same picker ID for serialized events, captured events and manual choices.
StringName source_id(const Ref<InputEvent> &event, InputBinding::Stick stick, InputBinding::MouseAxes mouse_axes) {
    if (stick == InputBinding::LEFT_STICK) {
        return "stick/left";
    }
    if (stick == InputBinding::RIGHT_STICK) {
        return "stick/right";
    }
    if (const Ref<InputEventKey> key = event; key.is_valid()) {
        const auto code = key->get_physical_keycode() != KEY_NONE ? key->get_physical_keycode() : key->get_keycode();
        return StringName(String("key/") + String::num_int64(code));
    }
    if (const Ref<InputEventMouseButton> mouse = event; mouse.is_valid()) {
        return StringName(String("mouse/") + String::num_int64(mouse->get_button_index()));
    }
    if (const Ref<InputEventJoypadButton> pad = event; pad.is_valid()) {
        return StringName(String("pad/") + String::num_int64(pad->get_button_index()));
    }
    if (const Ref<InputEventJoypadMotion> axis = event; axis.is_valid()) {
        return StringName(String("axis/") + String::num_int64(axis->get_axis()) +
                (axis->get_axis_value() < 0.0f ? "/negative" : "/positive"));
    }
    if (Ref<InputEventMouseMotion>(event).is_valid()) {
        switch (mouse_axes) {
            case InputBinding::MOUSE_X: return "mouse/x";
            case InputBinding::MOUSE_Y: return "mouse/y";
            default: return "mouse/xy";
        }
    }
    return "none";
}

// Normalize one captured physical source for reuse across devices.
Ref<InputEvent> normalize_capture(const Ref<InputEvent> &event, InputAction::ValueKind kind) {
    if (kind == InputAction::MOTION) {
        const Ref<InputEventMouseMotion> motion = event;
        if (motion.is_valid() && motion->get_relative().length_squared() >= 16.0) {
            Ref<InputEventMouseMotion> saved;
            saved.instantiate();
            return saved;
        }
        return {};
    }
    if (const Ref<InputEventJoypadMotion> axis = event; axis.is_valid()) {
        const float value = axis->get_axis_value();
        if (!std::isfinite(value) || std::abs(value) < 0.65f) {
            return {};
        }
        Ref<InputEventJoypadMotion> saved;
        saved.instantiate();
        saved->set_device(-1);
        saved->set_axis(axis->get_axis());
        saved->set_axis_value(value < 0 ? -1 : 1);
        return saved;
    }
    if (!event->is_pressed() || event->is_echo() || event->is_canceled() || !valid_binding_event(event, kind)) {
        return {};
    }
    if (const Ref<InputEventKey> key = event; key.is_valid()) {
        Ref<InputEventKey> saved;
        saved.instantiate();
        saved->set_physical_keycode(key->get_physical_keycode());
        if (key->get_physical_keycode() == KEY_NONE) {
            saved->set_keycode(key->get_keycode());
        }
        saved->set_alt_pressed(key->is_alt_pressed() && key->get_keycode() != KEY_ALT);
        saved->set_ctrl_pressed(key->is_ctrl_pressed() && key->get_keycode() != KEY_CTRL);
        saved->set_shift_pressed(key->is_shift_pressed() && key->get_keycode() != KEY_SHIFT);
        saved->set_meta_pressed(key->is_meta_pressed() && key->get_keycode() != KEY_META);
        return saved;
    }
    if (const Ref<InputEventMouseButton> mouse = event; mouse.is_valid()) {
        Ref<InputEventMouseButton> saved;
        saved.instantiate();
        saved->set_button_index(mouse->get_button_index());
        saved->set_alt_pressed(mouse->is_alt_pressed());
        saved->set_ctrl_pressed(mouse->is_ctrl_pressed());
        saved->set_shift_pressed(mouse->is_shift_pressed());
        saved->set_meta_pressed(mouse->is_meta_pressed());
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

// Select the source scope for a complete controller stick.
InputBinding::Stick full_stick(const Ref<InputEvent> &event, InputAction::ValueKind kind) {
    const Ref<InputEventJoypadMotion> axis = event;
    if (kind != InputAction::VECTOR || axis.is_null() || !std::isfinite(axis->get_axis_value()) ||
            std::abs(axis->get_axis_value()) < 0.65f) {
        return InputBinding::NONE;
    }
    switch (axis->get_axis()) {
        case JOY_AXIS_LEFT_X:
        case JOY_AXIS_LEFT_Y:
            return InputBinding::LEFT_STICK;
        case JOY_AXIS_RIGHT_X:
        case JOY_AXIS_RIGHT_Y:
            return InputBinding::RIGHT_STICK;
        default:
            return InputBinding::NONE;
    }
}

// Older bindings can carry a scale or direction ahead of their modifier stack.
bool has_legacy_contribution(const InputBinding &binding, InputAction::ValueKind kind) {
    if (kind == InputAction::BUTTON || binding.get_stick() != InputBinding::NONE) {
        return false;
    }
    const Vector3 contribution = binding.get_contribution();
    return kind == InputAction::MOTION ? Ref<InputEventMouseMotion>(binding.get_event()).is_valid() &&
                    (contribution.x != 1 || contribution.y != 1) :
            contribution.x != 1 || contribution.y != 0;
}

// Only offer migration if the same raw value can be represented without combining axes.
bool can_convert_contribution(const InputBinding &binding, InputAction::ValueKind kind) {
    const Vector3 contribution = binding.get_contribution();
    return has_legacy_contribution(binding, kind) && contribution.is_finite() &&
            (kind == InputAction::MOTION || contribution.y == 0 ||
                    (kind == InputAction::VECTOR && contribution.x == 0));
}

// Present the supported modifier by name in both its settings card and removal dialog.
String modifier_title(const Ref<InputModifier> &modifier) {
    if (Ref<InputDeadZone>(modifier).is_valid()) {
        return "Dead Zone";
    }
    if (Ref<InputSwapAxis>(modifier).is_valid()) {
        return "Swap Axis";
    }
    if (Ref<InputInvert>(modifier).is_valid()) {
        return "Invert";
    }
    if (Ref<InputScale>(modifier).is_valid()) {
        return "Scale";
    }
    if (Ref<InputResponseCurve>(modifier).is_valid()) {
        return "Response Curve";
    }
    return Ref<InputNormalize>(modifier).is_valid() ? String("Normalize") : String("Unknown Modifier");
}

// Apply a stable-ID move either by one step or before/after a sibling.
template <class T>
bool move_order_item(TypedArray<T> &items, const StringName &source, const StringName &target, int placement) {
    int64_t from = -1;
    int64_t other = -1;
    for (int64_t index = 0; index < items.size(); ++index) {
        const Ref<T> entry = items[index];
        if (entry.is_valid()) {
            if (entry->get_id() == source) {
                from = index;
            }
            if (entry->get_id() == target) {
                other = index;
            }
        }
    }
    if (from < 0 || (!target.is_empty() && other < 0)) {
        return false;
    }
    const int64_t boundary = other + (placement > 0 ? 1 : 0);
    const int64_t to = target.is_empty() ? from + placement : boundary - (from < boundary ? 1 : 0);
    if (to < 0 || to >= items.size() || to == from) {
        return false;
    }
    const Variant held = items[from];
    items.remove_at(from);
    items.insert(to, held);
    return true;
}

} // namespace

void InputMapWindow::_bind_methods() {}

void InputMapWindow::build() {
    const auto *editor = EditorInterface::get_singleton();
    set_name("CrimsonInputMap");
    set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    set_title("Input Map");
    set_visible(false);
    set_transient(true);
    set_force_native(editor->is_multi_window_enabled());
    ui::embed_editor_popups(this);
    set_theme(ui::create_theme(this, editor->get_editor_scale()));
    set_min_size(Vector2i(ui::pixels(this, 620), ui::pixels(this, 400)));
    set_size(Vector2i(ui::pixels(this, 996), ui::pixels(this, 554)));
    connect("close_requested", callable_mp(this, &InputMapWindow::close_window));
    connect("focus_exited", callable_mp(this, &InputMapWindow::cancel_listening));

    auto *frame = ui::surface_panel(this, ui::Surface::CANVAS);
    frame->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    auto *root = add<VBoxContainer>(frame);
    root->add_theme_constant_override("separation", 0);
    auto *workspace_inset = inset(root, spacing::md, spacing::md, spacing::md);
    workspace_inset->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    auto *workspace = add<PanelContainer>(workspace_inset);
    const double editor_scale = get_theme_default_base_scale();
    const Color transparent(0, 0, 0, 0);
    workspace->add_theme_stylebox_override("panel", ui::rounded_box(ui::color(ui::Surface::HEADER),
            ui::color(ui::Surface::RULE), 1, 1, editor_scale, ui::popup_corner_radius));
    auto *left = memnew(VBoxContainer);
    auto *binding_pane = memnew(PanelContainer);
    auto rail = ui::box(ui::color(ui::Surface::SIDEBAR), transparent);
    rail->set_corner_radius(CORNER_TOP_RIGHT, ui::pixels(this, ui::popup_corner_radius - 1));
    rail->set_corner_radius(CORNER_BOTTOM_RIGHT, ui::pixels(this, ui::popup_corner_radius - 1));
    rail->set_anti_aliased(true);
    binding_pane->add_theme_stylebox_override("panel", rail);
    auto *split = ui::split_pane(workspace, left, binding_pane);
    auto divider = ui::box(transparent, ui::color(ui::Surface::RULE));
    divider->set_border_width_all(0);
    divider->set_border_width(SIDE_RIGHT, ui::pixels(this, 1));
    split->add_theme_stylebox_override("split_bar_background", divider);
    left->set_custom_minimum_size(Vector2(ui::pixels(this, 300), 0));
    binding_pane->set_custom_minimum_size(Vector2(ui::pixels(this, 258), 0));
    left->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    binding_pane->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    left->set_stretch_ratio(1.65);
    binding_pane->set_stretch_ratio(1.0);
    left->add_theme_constant_override("separation", 0);

    auto *list_header = add<VBoxContainer>(inset(left, spacing::lg, spacing::md, spacing::md));
    list_header->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *list_title = add<HBoxContainer>(list_header);
    list_title->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *actions_title = ui::label(list_title, "Actions", ui::LabelStyle::TITLE);
    actions_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    auto *action_menu = ui::menu(list_title, "Add Action", "Add an existing InputAction asset or create a new definition.");
    action_menu->set_name("AddInputAction");
    action_menu->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    action_menu->get_popup()->add_item(String(U"Choose Action Asset\u2026"), 0);
    action_menu->get_popup()->add_item(String(U"New Action Asset\u2026"), 1);
    action_menu->get_popup()->connect("id_pressed", callable_mp(this, &InputMapWindow::action_command).bind(StringName()));
    search = ui::search_input(list_header, "Filter actions or bindings", "Find an action by name, script ID or physical control.");
    search->set_name("InputMapSearch");
    search->connect("text_changed", callable_mp(this, &InputMapWindow::filter));
    rule(left);
    auto *list_body = inset(left, spacing::md, spacing::xs, spacing::md);
    list_body->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    binding_scroll = add<ScrollContainer>(list_body);
    binding_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    binding_scroll->set_follow_focus(true);
    binding_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    bindings = ui::tree(binding_scroll, callable_mp(this, &InputMapWindow::select_row));
    bindings->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    bindings->set_name("ActionBindingTree");
    binding_drag_rail = ui::drag_insertion_rail(left);

    modifiers = add<VBoxContainer>(binding_pane);
    modifiers->set_name("BindingModifiers");
    modifier_drag_rail = ui::drag_insertion_rail(binding_pane);
    modifiers->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    modifiers->add_theme_constant_override("separation", 0);

    rule(root);
    auto *footer_row = add<HBoxContainer>(inset(root, spacing::lg, spacing::md, spacing::md));
    footer_row->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    counts = ui::label(footer_row, "", ui::LabelStyle::MUTED);
    status = ui::label(footer_row, "", ui::LabelStyle::MUTED);
    status->set_name("InputStatus");
    status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    status->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    clip(status);
    auto *map_menu = ui::menu(footer_row, "Map", "Create, open, duplicate, rename, or undo edits to an input map.");
    map_menu->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    map_commands = map_menu->get_popup();
    map_commands->add_item(String(U"New Map\u2026"), NEW_MAP);
    map_commands->add_item(String(U"Open Map\u2026"), OPEN_MAP);
    map_commands->add_item("Open FPS Starter", OPEN_PRESET);
    map_commands->add_separator();
    map_commands->add_item(String(U"Map Settings\u2026"), MAP_SETTINGS);
    map_commands->add_item(String(U"Save a Copy\u2026"), SAVE_COPY);
    map_commands->add_separator();
    map_commands->add_item("Undo", UNDO_DRAFT);
    map_commands->add_item("Redo", REDO_DRAFT);
    map_commands->connect("id_pressed", callable_mp(this, &InputMapWindow::map_command));
    save_button = ui::button(footer_row, "Save", "Save this map resource (Ctrl+S).",
            callable_mp(this, &InputMapWindow::save), ui::ButtonStyle::PRIMARY);
    save_button->set_custom_minimum_size(Vector2(ui::pixels(this, 64), ui::pixels(this, 28)));
    save_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);

    file_dialog = add<EditorFileDialog>(this);
    ui::style_file_dialog(file_dialog);
    file_dialog->set_access(FileDialog::ACCESS_RESOURCES);
    file_dialog->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
    file_dialog->add_filter("*.tres", "Godot text resource");
    file_dialog->connect("file_selected", callable_mp(this, &InputMapWindow::file_selected));
    file_dialog->connect("canceled", callable_mp(this, &InputMapWindow::cancel_file_selection));

    discard_dialog = ui::editor_modal(this, frame, callable_mp(this, &InputMapWindow::confirm_discard),
            callable_mp(this, &InputMapWindow::cancel_discard));
    discard_dialog->set_name("DiscardMapDraft");
    discard_dialog->set_confirm_text("Discard and Continue");
    discard_dialog->set_destructive(true);

    remove_dialog = ui::removal_confirmation(this, frame, callable_mp(this, &InputMapWindow::confirm_removal),
            callable_mp(this, &InputMapWindow::cancel_removal));

    action_dialog = ui::editor_modal(this, frame, callable_mp(this, &InputMapWindow::save_new_action));
    action_dialog->set_name("NewInputAction");
    action_dialog->set_confirm_text(String(U"Save Action Asset\u2026"));
    action_dialog->set_validator(callable_mp(this, &InputMapWindow::validate_action_name));
    auto *action_fields = action_dialog->body();
    action_name = ui::text_input(nullptr, "", "Display name shared wherever this action definition is used.",
            ui::FieldState::EDITABLE, "e.g. Interact");
    ui::field(action_fields, "Action name", action_name, action_name->get_tooltip_text());
    action_kind = ui::select(nullptr, value_kinds(), 0, {}, ui::FieldState::EDITABLE, ui::SelectStyle::MENU);
    action_kind->set_name("ActionValueType");
    const String type_help = "Choose the value this action supplies to gameplay. Mouse Motion delivers relative pointer pixels.";
    ui::field(action_fields, "Value type", action_kind, type_help);
    ui::tooltip(action_kind->get_trigger(), type_help);
    action_dialog->register_text_enter(action_name);

    action_picker = ui::editor_modal(this, frame, callable_mp(this, &InputMapWindow::confirm_action_choice));
    action_picker->set_name("ChooseInputAction");
    action_picker->set_confirm_text("Add Action");
    action_picker->set_validator(callable_mp(this, &InputMapWindow::validate_action_choice));
    auto *action_lookup = action_picker->body();
    action_query = ui::search_input(action_lookup, "Search InputAction assets", "Filter project action assets by name or folder.");
    action_query->set_name("InputActionAssetQuery");
    action_query->connect("text_changed", callable_mp(this, &InputMapWindow::filter_action_assets));
    action_query->connect("gui_input", callable_mp(this, &InputMapWindow::navigate_action_results));
    action_picker->register_text_enter(action_query);
    action_results = add<Tree>(action_lookup);
    action_results->set_name("InputActionAssetResults");
    action_results->set_accessibility_name("InputAction assets");
    action_results->set_custom_minimum_size(Vector2(0, ui::pixels(this, 156)));
    action_results->set_hide_root(true);
    action_results->set_hide_folding(true);
    action_results->set_select_mode(Tree::SELECT_ROW);
    action_results->set_columns(2);
    for (int column = 0; column < 2; ++column) {
        action_results->set_column_clip_content(column, true);
        action_results->set_column_expand_ratio(column, column == 0 ? 4 : 5);
    }
    action_results->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    action_results->connect("item_activated", callable_mp(action_picker, &ui::EditorModal::confirm));
    if (auto *filesystem = editor->get_resource_filesystem()) {
        filesystem->connect("filesystem_changed", callable_mp(this, &InputMapWindow::read_action_assets));
        filesystem->connect("sources_changed", callable_mp(this, &InputMapWindow::finish_action_scan), Object::CONNECT_DEFERRED);
    }

    metadata_dialog = ui::editor_modal(this, frame, callable_mp(this, &InputMapWindow::apply_metadata));
    metadata_dialog->set_name("InputMapMetadata");
    metadata_dialog->set_confirm_text("Apply");
    metadata_dialog->set_validator(callable_mp(this, &InputMapWindow::validate_metadata));
    auto *metadata_fields = metadata_dialog->body();
    metadata_title = ui::text_input(nullptr);
    ui::field(metadata_fields, "Display name", metadata_title, "The friendly name displayed in the editor.");
    metadata_id = ui::text_input(nullptr);
    ui::field(metadata_fields, "ID", metadata_id, "The stable name used by gameplay code and Controls.");
    metadata_dialog->register_text_enter(metadata_id);
    build_choices();
}

Ref<InputActionMap> InputMapWindow::snapshot(const Ref<InputActionMap> &map) {
    if (map.is_null()) {
        return InputActionMap::create(InputAction::DEFAULT_MAP);
    }
    Ref<InputActionMap> next = detached(map);
    TypedArray<InputActionMapping> mappings;
    for (const Variant &value : map->get_mappings()) {
        const Ref<InputActionMapping> original = value;
        auto mapping = detached(original);
        if (mapping.is_valid()) {
            TypedArray<InputBinding> slots;
            for (const Variant &slot_value : original->get_bindings()) {
                const Ref<InputBinding> previous = slot_value;
                const auto slot = detached(previous);
                if (slot.is_valid()) {
                    const auto event = previous->get_event();
                    if (event.is_valid()) {
                        slot->set_event(copy_binding_event(event));
                    }
                    TypedArray<InputModifier> transforms;
                    for (const Variant &modifier : previous->get_modifiers()) {
                        transforms.append(detached(Ref<InputModifier>(modifier)));
                    }
                    slot->set_modifiers(transforms);
                }
                slots.append(slot);
            }
            mapping->set_bindings(slots);
        }
        mappings.append(mapping);
    }
    next->set_mappings(mappings);
    return next;
}

Ref<InputBinding> InputMapWindow::find_binding(const StringName &id, Ref<InputActionMapping> *mapping) const {
    if (draft.is_valid() && !id.is_empty()) {
        for (const Variant &value : draft->get_mappings()) {
            const Ref<InputActionMapping> entry = value;
            if (entry.is_null()) {
                continue;
            }
            for (const Variant &item : entry->get_bindings()) {
                const Ref<InputBinding> binding = item;
                if (binding.is_valid() && binding->get_id() == id) {
                    if (mapping) {
                        *mapping = entry;
                    }
                    return binding;
                }
            }
        }
    }
    return {};
}

Ref<InputActionMapping> InputMapWindow::find_mapping(const StringName &id) const {
    for (const Variant &value : draft->get_mappings()) {
        const Ref<InputActionMapping> mapping = value;
        if (mapping.is_valid() && mapping->get_id() == id) {
            return mapping;
        }
    }
    return {};
}

void InputMapWindow::open(const Ref<InputActionMap> &map, const Ref<InputLayout> &layout, Object *owner, bool resume) {
    const ObjectID owner_id(owner ? owner->get_instance_id() : 0);
    if (!is_visible()) {
        popup_centered_clamped(get_size(), 0.92f);
    } else {
        grab_focus();
    }
    if (!resume || draft.is_null()) {
        const bool source_changed = map.is_valid() && revision_of(map) != source_revision;
        const bool switching = map != source || layout != assignment_layout || owner_id != assignment_owner || source_changed;
        if (switching) {
            if (auto *modal = active_modal(); modal && modal != discard_dialog) {
                modal->cancel();
            }
        }
        if (draft.is_valid() && draft != saved && switching) {
            next_source = map;
            next_layout = layout;
            next_owner = owner_id;
            discard_dialog->open("Discard map draft?", "This map has unsaved edits.");
            return;
        }
        if (draft.is_null() || switching || draft == saved) {
            load_map(map, layout, owner_id);
        }
    }
}

void InputMapWindow::load_map(const Ref<InputActionMap> &map, const Ref<InputLayout> &layout, ObjectID owner) {
    cancel_listening();
    source = map;
    draft = snapshot(map);
    saved = draft;
    source_revision = revision_of(source);
    assignment_layout = layout;
    assignment_owner = owner;
    history = {draft};
    revision = 0;
    selected_slot = StringName();
    focused_row = StringName();
    picker_focus_slot = StringName();
    collapsed.clear();
    rendered_actions.clear();
    bindings->clear();
    loading_map = true;
    search->clear();
    loading_map = false;

    bool found_modified = false;
    for (const Variant &entry : draft->get_mappings()) {
        const Ref<InputActionMapping> mapping = entry;
        if (mapping.is_null()) {
            continue;
        }
        for (const Variant &slot : mapping->get_bindings()) {
            const Ref<InputBinding> binding = slot;
            if (binding.is_valid() && (selected_slot.is_empty() || !binding->get_modifiers().is_empty())) {
                selected_slot = binding->get_id();
                if (!binding->get_modifiers().is_empty()) {
                    found_modified = true;
                    break;
                }
            }
        }
        if (found_modified) {
            break;
        }
    }
    status->set_text("");
    redraw();
}

void InputMapWindow::stage(const Ref<InputActionMap> &next, bool rebuild_rows, bool rebuild_modifiers) {
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
    status->set_text("");
    if (rebuild_rows) {
        queue_redraw();
    } else {
        refresh_frame();
        if (rebuild_modifiers) {
            queue_modifier_refresh();
        }
    }
}

void InputMapWindow::queue_redraw() {
    if (!redraw_pending) {
        redraw_pending = true;
        callable_mp(this, &InputMapWindow::redraw).call_deferred();
    }
}

void InputMapWindow::queue_modifier_refresh() {
    if (!modifier_refresh_pending) {
        modifier_refresh_pending = true;
        callable_mp(this, &InputMapWindow::draw_modifiers).call_deferred();
    }
}

void InputMapWindow::refresh_frame() {
    const String heading = map_title(draft);
    set_title(String(U"Input Map \u00B7 ") + heading + (draft != saved ? String(" *") : String()));
    map_commands->set_item_disabled(map_commands->get_item_index(UNDO_DRAFT), revision == 0);
    map_commands->set_item_disabled(map_commands->get_item_index(REDO_DRAFT), revision + 1 >= history.size());
    save_button->set_disabled(draft.is_null());

    if (draft.is_null()) {
        return;
    }
    int64_t total = 0;
    for (const Variant &entry : draft->get_mappings()) {
        const Ref<InputActionMapping> mapping = entry;
        if (mapping.is_valid()) {
            total += mapping->get_bindings().size();
        }
    }
    counts->set_text(String::num_int64(draft->get_mappings().size()) + String(U" actions \u00B7 ") +
            String::num_int64(total) + " bindings");
    const Error validation = draft->validate();
    if (validation != OK) {
        status->set_text(validation == ERR_ALREADY_EXISTS ? "Resolve repeated action or binding IDs before saving." :
                "Resolve invalid action, binding or modifier settings before saving.");
    }
}

void InputMapWindow::redraw() {
    redraw_pending = false;
    if (draft.is_null()) {
        return;
    }
    const auto entries = draft->get_mappings();
    if (!displayed_filter) {
        for (const auto &id : rendered_actions) {
            if (bindings->is_expanded(id)) {
                collapsed.erase(id);
            } else {
                collapsed.insert(id);
            }
        }
    }
    rendered_actions.clear();
    const int previous_scroll = binding_scroll->get_v_scroll();
    bindings->clear();
    const String query = search->get_text().strip_edges().to_lower();
    displayed_filter = !query.is_empty();
    int visible_actions = 0;
    for (const Variant &value : entries) {
        const Ref<InputActionMapping> mapping = value;
        if (mapping.is_null()) {
            continue;
        }
        const String action_name_text = action_title(mapping);
        const bool show_all = query.is_empty() || action_name_text.to_lower().contains(query) ||
                String(mapping->get_id()).to_lower().contains(query);
        std::vector<Ref<InputBinding>> visible_bindings;
        for (const Variant &slot_value : mapping->get_bindings()) {
            const Ref<InputBinding> binding = slot_value;
            if (binding.is_valid() && (show_all || binding_label(binding).to_lower().contains(query) ||
                    String(binding->get_id()).to_lower().contains(query))) {
                visible_bindings.push_back(binding);
            }
        }
        if (!show_all && visible_bindings.empty()) {
            continue;
        }
        ++visible_actions;

        const auto action = mapping->get_action();
        auto *heading = memnew(HBoxContainer);
        heading->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
        auto *name = ui::label(heading, action_name_text, ui::LabelStyle::HEADING);
        name->add_theme_font_size_override("font_size", ui::pixels(this, 13));
        name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        name->set_tooltip_text(action_name_text + String("\nAction ID: ") + String(mapping->get_id()));
        name->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        clip(name);
        const int kind = action.is_valid() ? static_cast<int>(action->get_kind()) : -1;
        const auto kinds = value_kinds();
        auto *value_type = ui::label(heading, kind >= 0 && kind < kinds.size() ? kinds[kind] : String("Missing Action"),
                ui::LabelStyle::MUTED);
        value_type->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
        auto *actions = memnew(HBoxContainer);
        actions->add_theme_constant_override("separation", ui::pixels(this, spacing::xs));
        ui::icon_button(actions, "add", "Add a physical binding to " + action_name_text + ".",
                callable_mp(this, &InputMapWindow::add_binding).bind(mapping->get_id()), ui::ButtonStyle::QUIET);
        auto *options = ui::menu(actions, String(U"\u2026"), "Edit this map entry or its reusable action definition.");
        options->set_theme_type_variation("CrimsonQuiet");
        options->set_custom_minimum_size(Vector2(ui::pixels(this, 28), ui::pixels(this, 28)));
        options->set_button_icon({});
        options->set_accessibility_name("Action options");
        auto *menu = options->get_popup();
        menu->add_item(String(U"Edit Action ID\u2026"), 2);
        menu->add_item("Inspect Action Asset", 3);
        menu->set_item_disabled(1, action.is_null());
        menu->add_separator();
        menu->add_item("Remove Action", 4);
        menu->connect("id_pressed", callable_mp(this, &InputMapWindow::action_command).bind(mapping->get_id()));
        const StringName parent = row_id(mapping->get_id(), false);
        bindings->add_row(parent, heading, {}, actions, displayed_filter || collapsed.find(parent) == collapsed.end(),
                String("Action ID: ") + String(mapping->get_id()));
        if (!displayed_filter && entries.size() > 1) {
            auto *handle = add_row_order_handle(heading, mapping->get_id(), {}, action_name_text);
            heading->move_child(handle, 0);
            auto *background = Object::cast_to<Control>(heading->get_parent()->get_parent());
            name->set_drag_forwarding(
                    callable_mp(this, &InputMapWindow::begin_row_drag).bind(handle, mapping->get_id(), StringName(), action_name_text),
                    callable_mp(this, &InputMapWindow::can_drop_row).bind(mapping->get_id(), StringName(), name, background),
                    callable_mp(this, &InputMapWindow::drop_row).bind(mapping->get_id(), StringName(), name));
        }
        if (!visible_bindings.empty()) {
            rendered_actions.insert(parent);
        }
        for (const auto &binding : visible_bindings) {
            build_binding(binding, mapping, parent);
        }
    }
    if (visible_actions == 0) {
        ui::empty_state(bindings, entries.is_empty() ? "No actions" : "No matching actions",
                entries.is_empty() ? "Use Add Action to choose or create an action asset." : "Try a different action name or physical control.");
    }
    if (find_binding(selected_slot).is_null()) {
        selected_slot = StringName();
    }
    if (!selected_slot.is_empty()) {
        bindings->set_selected(row_id(selected_slot, true), false);
    }
    if (!focused_row.is_empty()) {
        bindings->set_selected(focused_row, true);
        focused_row = StringName();
    } else if (!displayed_filter) {
        binding_scroll->set_deferred("scroll_vertical", previous_scroll);
    }
    refresh_frame();
    draw_modifiers();
}

void InputMapWindow::filter(const String &) {
    if (loading_map) {
        return;
    }
    cancel_listening();
    queue_redraw();
}

void InputMapWindow::select_row(const StringName &row) {
    const String id = row;
    if (id.begins_with("binding/")) {
        selected_slot = StringName(id.substr(8));
        bindings->set_selected(row, false);
    } else {
        selected_slot = StringName();
    }
    status->set_text("");
    queue_modifier_refresh();
}

void InputMapWindow::build_choices() {
    auto append = [this](const String &label, const String &group, const Ref<InputEvent> &event,
                          InputBinding::Stick stick = InputBinding::NONE, InputBinding::MouseAxes mouse_axes = InputBinding::MOUSE_XY) {
        choices.push_back({source_id(event, stick, mouse_axes), label, group, event, stick, mouse_axes});
    };
    append("None", "Unassigned", {});
    // Include the complete range of standard physical keyboard positions that the OS can label.
    auto add_key = [&](Key code) {
        const String label = editor_labels::key_name(code);
        if (!label.is_empty()) {
            Ref<InputEventKey> event;
            event.instantiate();
            event->set_physical_keycode(code);
            append(label, "Keyboard", event);
        }
    };
    for (int i = KEY_SPACE; i <= KEY_ASCIITILDE; ++i) {
        add_key(static_cast<Key>(i));
    }
    for (int i = KEY_ESCAPE; i <= KEY_F35; ++i) {
        add_key(static_cast<Key>(i));
    }
    for (Key key : extra_keys) {
        add_key(key);
    }
    for (int i = KEY_KP_MULTIPLY; i <= KEY_KP_9; ++i) {
        add_key(static_cast<Key>(i));
    }
    Ref<InputEventMouseMotion> motion;
    motion.instantiate();
    append("Mouse X (Horizontal)", "Mouse", motion, InputBinding::NONE, InputBinding::MOUSE_X);
    append("Mouse Y (Vertical)", "Mouse", motion, InputBinding::NONE, InputBinding::MOUSE_Y);
    append("Mouse XY (Both)", "Mouse", motion);
    for (int i = MOUSE_BUTTON_LEFT; i <= MOUSE_BUTTON_XBUTTON2; ++i) {
        Ref<InputEventMouseButton> event;
        event.instantiate();
        event->set_button_index(static_cast<MouseButton>(i));
        append(editor_labels::mouse_button_name(event->get_button_index()), "Mouse", event);
    }
    append("Left Stick", "Gamepad", {}, InputBinding::LEFT_STICK);
    append("Right Stick", "Gamepad", {}, InputBinding::RIGHT_STICK);
    for (int i = 0; i < JOY_BUTTON_SDL_MAX; ++i) {
        Ref<InputEventJoypadButton> event;
        event.instantiate();
        event->set_device(-1);
        event->set_button_index(static_cast<JoyButton>(i));
        append(editor_labels::gamepad_button_name(event->get_button_index()), "Gamepad", event);
    }
    for (int i = 0; i < JOY_AXIS_SDL_MAX; ++i) {
        for (int sign : {-1, 1}) {
            if (sign < 0 && i >= JOY_AXIS_TRIGGER_LEFT) {
                continue;
            }
            Ref<InputEventJoypadMotion> event;
            event.instantiate();
            event->set_device(-1);
            event->set_axis(static_cast<JoyAxis>(i));
            event->set_axis_value(sign);
            append(editor_labels::gamepad_axis_name(event->get_axis(), sign < 0), "Gamepad", event);
        }
    }
}

StringName InputMapWindow::choice_id(const Ref<InputBinding> &binding) const {
    const auto event = binding->get_event();
    const Ref<InputEventWithModifiers> chord = event;
    if ((chord.is_valid() && chord->get_modifiers_mask() != 0) || (is_gamepad(event) && event->get_device() >= 0)) {
        return "current";
    }
    return source_id(event, binding->get_stick(), binding->get_mouse_axes());
}

String InputMapWindow::binding_label(const Ref<InputBinding> &binding) const {
    const auto id = choice_id(binding);
    const auto found = std::find_if(choices.begin(), choices.end(), [&](const SourceChoice &choice) { return choice.id == id; });
    return found != choices.end() ? found->label : editor_labels::control_name(binding->get_event());
}

void InputMapWindow::build_binding(const Ref<InputBinding> &binding, const Ref<InputActionMapping> &mapping, const StringName &parent) {
    const StringName id = binding->get_id();
    auto *body = memnew(HBoxContainer);
    body->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *device = ui::icon_button(body, device_icon(binding), "Listen for input. Escape cancels the capture.",
            {}, ui::ButtonStyle::QUIET);
    Ref<StyleBoxEmpty> device_feedback;
    device_feedback.instantiate();
    for (const char *state : {"hover", "pressed", "focus"}) {
        device->add_theme_stylebox_override(state, device_feedback);
    }
    device->add_theme_color_override("icon_hover_color", ui::color(ui::Surface::TEXT));
    device->set_custom_minimum_size(Vector2(ui::pixels(this, 28), ui::pixels(this, 28)));
    device->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    device->connect("pressed", callable_mp(this, &InputMapWindow::start_listening).bind(id, device));
    auto *picker = ui::picker(body, binding_label(binding), callable_mp(this, &InputMapWindow::choose_binding).bind(id), true);
    picker->set_custom_minimum_size(Vector2(ui::pixels(this, 98), 0));
    picker->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    picker->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    picker->set_popup_width_limit(328);
    auto *trigger = picker->get_trigger();
    trigger->set_theme_type_variation("CrimsonQuiet");
    trigger->set_accessibility_name("Input source: " + binding_label(binding));
    trigger->set_tooltip_text(binding_label(binding) + "\nChoose a physical control without using an attached device.");
    trigger->connect("pressed", callable_mp(this, &InputMapWindow::select_row).bind(row_id(id, true)));
    const auto action = mapping->get_action();
    const auto kind = action.is_valid() ? action->get_kind() : InputAction::BUTTON;
    std::vector<ui::PickerOption> items;
    const auto current_choice = choice_id(binding);
    const auto current_label = binding_label(binding);
    for (const auto &choice : choices) {
        if ((choice.stick == InputBinding::NONE && valid_binding_event(choice.event, kind)) ||
                (choice.stick != InputBinding::NONE && kind == InputAction::VECTOR)) {
            items.push_back({choice.id, choice.id == current_choice ? current_label : choice.label, choice.group, nullptr});
        }
    }
    if (std::none_of(items.begin(), items.end(), [&](const auto &item) { return item.id == current_choice; })) {
        items.push_back({current_choice, current_label, "Current", nullptr});
    }
    picker->set_options(std::move(items));
    picker->set_selected(current_choice);
    if (picker_focus_slot == id) {
        picker->get_trigger()->call_deferred("grab_focus");
        picker_focus_slot = StringName();
    }
    auto *remove = ui::icon_button(nullptr, "remove", "Remove this binding.", callable_mp(this, &InputMapWindow::prompt_binding_removal).bind(id),
            ui::ButtonStyle::QUIET);
    remove->set_custom_minimum_size(Vector2(ui::pixels(this, 28), ui::pixels(this, 28)));
    bindings->add_row(row_id(id, true), body, parent, remove, false, current_label);
    if (!displayed_filter && mapping->get_bindings().size() > 1) {
        auto *handle = add_row_order_handle(body, id, mapping->get_id(), current_label);
        body->move_child(handle, 0);
        auto *background = Object::cast_to<Control>(body->get_parent()->get_parent());
        trigger->set_drag_forwarding({}, callable_mp(this, &InputMapWindow::can_drop_row).bind(id, mapping->get_id(), trigger, background),
                callable_mp(this, &InputMapWindow::drop_row).bind(id, mapping->get_id(), trigger));
    }
}

Button *InputMapWindow::add_row_order_handle(Node *owner, StringName item, StringName parent, const String &caption) {
    const String help = String("Drag to reorder ") + caption + ". Alt+Up and Alt+Down move it with the keyboard.";
    auto *handle = ui::drag_grip(owner, help);
    handle->set_name(parent.is_empty() ? "ActionOrderHandle" : "BindingOrderHandle");
    handle->set_accessibility_name(String("Reorder ") + caption);
    auto *background = Object::cast_to<Control>(owner->get_parent()->get_parent());
    handle->set_drag_forwarding(callable_mp(this, &InputMapWindow::begin_row_drag).bind(handle, item, parent, caption),
            callable_mp(this, &InputMapWindow::can_drop_row).bind(item, parent, handle, background),
            callable_mp(this, &InputMapWindow::drop_row).bind(item, parent, handle));
    handle->connect("gui_input", callable_mp(this, &InputMapWindow::shortcut_row_order).bind(item, parent, handle));
    if (row_id(item, !parent.is_empty()) == order_focus_row) {
        order_focus_row = {};
        handle->call_deferred("grab_focus");
    }
    return handle;
}

void InputMapWindow::shortcut_row_order(const Ref<InputEvent> &event, StringName item, StringName parent, Button *handle) {
    const Ref<InputEventKey> key = event;
    if (key.is_null() || !key->is_pressed() || key->is_echo() || !key->is_alt_pressed() || key->is_command_or_control_pressed()) {
        return;
    }
    const Key code = key->get_keycode();
    if (code != KEY_UP && code != KEY_DOWN) {
        return;
    }
    handle->accept_event();
    reorder_row(item, parent, {}, code == KEY_UP ? -1 : 1);
}

void InputMapWindow::_notification(int what) {
    if (what == NOTIFICATION_DRAG_END || what == NOTIFICATION_WM_MOUSE_EXIT || what == NOTIFICATION_WM_WINDOW_FOCUS_OUT) {
        if (binding_drag_rail) binding_drag_rail->hide();
        if (modifier_drag_rail) modifier_drag_rail->hide();
    }
}

Variant InputMapWindow::begin_row_drag(const Vector2 &, Button *handle, StringName item, StringName parent,
        const String &caption) {
    if (draft.is_null() || displayed_filter || active_modal()) {
        return {};
    }
    Dictionary data;
    data["kind"] = StringName("crimson_input_row");
    data["draft"] = draft;
    data["item"] = item;
    data["parent"] = parent;
    ui::show_drag_preview(handle, caption);
    return data;
}

StringName InputMapWindow::row_drag_source(const Variant &data, StringName parent) const {
    if (data.get_type() != Variant::DICTIONARY || displayed_filter || active_modal()) {
        return {};
    }
    const Dictionary payload = data;
    if (StringName(payload.get("kind", StringName())) != StringName("crimson_input_row") ||
            StringName(payload.get("parent", StringName())) != parent ||
            Ref<InputActionMap>(payload.get("draft", Variant())) != draft) {
        return {};
    }
    return payload.get("item", StringName());
}

bool InputMapWindow::can_drop_row(const Vector2 &point, const Variant &data, StringName item, StringName parent,
        Control *target, Control *background) const {
    const auto source = row_drag_source(data, parent);
    if (!source.is_empty() && source != item && point.is_finite()) {
        ui::place_drag_insertion_rail(binding_drag_rail, background->get_global_rect(), binding_scroll->get_global_rect(),
                point.y >= target->get_size().y * 0.5);
    }
    return !source.is_empty() && source != item;
}

void InputMapWindow::drop_row(const Vector2 &point, const Variant &data, StringName item, StringName parent, Control *target) {
    const auto source = row_drag_source(data, parent);
    if (!source.is_empty() && source != item) {
        reorder_row(source, parent, item, point.y >= target->get_size().y * 0.5 ? 1 : -1);
    }
}

void InputMapWindow::reorder_row(StringName source, StringName parent, StringName target, int placement) {
    if (draft.is_null() || displayed_filter || active_modal()) {
        return;
    }
    auto rows = draft->get_mappings();
    if (parent.is_empty()) {
        if (!move_order_item(rows, source, target, placement)) {
            return;
        }
    } else {
        const auto mapping = find_mapping(parent);
        const int64_t mapping_index = rows.find(mapping);
        if (mapping.is_null() || mapping_index < 0) {
            return;
        }
        auto slots = mapping->get_bindings();
        if (!move_order_item(slots, source, target, placement)) {
            return;
        }
        const auto replacement = detached(mapping);
        replacement->set_bindings(slots);
        rows[mapping_index] = replacement;
    }
    auto next = detached(draft);
    next->set_mappings(rows);
    order_focus_row = row_id(source, !parent.is_empty());
    stage(next);
}


void InputMapWindow::choose_binding(const StringName &choice, const StringName &slot) {
    selected_slot = slot;
    const auto binding = find_binding(slot);
    if (binding.is_null()) {
        return;
    }
    bindings->set_selected(row_id(slot, true), false);
    if (choice == choice_id(binding)) {
        queue_modifier_refresh();
        return;
    }
    const auto found = std::find_if(choices.begin(), choices.end(), [&](const SourceChoice &source_choice) {
        return source_choice.id == choice;
    });
    if (found != choices.end()) {
        picker_focus_slot = slot;
        set_binding_source(slot, copy_binding_event(found->event), found->stick, found->mouse_axes);
    }
}

void InputMapWindow::set_binding_source(const StringName &slot, const Ref<InputEvent> &event,
        InputBinding::Stick stick, InputBinding::MouseAxes mouse_axes) {
    Ref<InputActionMapping> owner;
    const auto binding = find_binding(slot, &owner);
    if (binding.is_null() || owner->get_action().is_null()) {
        return;
    }
    const auto kind = owner->get_action()->get_kind();
    if ((stick == InputBinding::NONE && !valid_binding_event(event, kind)) ||
            (stick != InputBinding::NONE && kind != InputAction::VECTOR)) {
        status->set_text("Choose a control compatible with this action type.");
        return;
    }
    const auto replacement = detached(binding);
    if (Ref<InputEventMouseMotion>(event).is_valid() &&
            (!Ref<InputEventMouseMotion>(binding->get_event()).is_valid() || binding->get_mouse_axes() != mouse_axes)) {
        const bool legacy_gain = has_legacy_contribution(*binding.ptr(), kind);
        const auto previous_axes = binding->get_mouse_axes();
        const auto previous_gain = binding->get_contribution();
        replacement->set_mouse_motion(mouse_axes);
        if (legacy_gain) {
            // Keep the gain on existing components. Newly enabled components start at unit scale.
            Vector2 factor(previous_axes == InputBinding::MOUSE_Y ? 1 : previous_gain.x,
                    previous_axes == InputBinding::MOUSE_X ? 1 : previous_gain.y);
            if (mouse_axes == InputBinding::MOUSE_X && previous_axes == InputBinding::MOUSE_XY && factor.x == 0) {
                factor.x = 1;
            } else if (mouse_axes == InputBinding::MOUSE_Y && previous_axes == InputBinding::MOUSE_XY && factor.y == 0) {
                factor.y = 1;
            }
            if (factor != Vector2(1, 1)) {
                Ref<InputScale> scale;
                scale.instantiate();
                scale->set_factor(factor);
                auto transforms = replacement->get_modifiers();
                transforms.insert(0, scale);
                replacement->set_modifiers(transforms);
            }
        }
    } else {
        replacement->set_event(event);
        replacement->set_stick(stick);
        replacement->set_mouse_axes(mouse_axes);
    }
    selected_slot = slot;
    stage(replace_binding(binding, replacement));
}

void InputMapWindow::add_binding(StringName mapping_id) {
    const auto previous = find_mapping(mapping_id);
    if (previous.is_null()) {
        return;
    }
    cancel_listening();
    auto next = detached(draft);
    auto entries = next->get_mappings();
    for (int64_t i = 0; i < entries.size(); ++i) {
        if (Ref<InputActionMapping>(entries[i]) != previous) {
            continue;
        }
        auto replacement = detached(previous);
        Ref<InputBinding> binding;
        binding.instantiate();
        const auto id = StringName(String(mapping_id) + "/" + Resource::generate_scene_unique_id());
        binding->set_id(id);
        replacement->add_binding(binding);
        entries[i] = replacement;
        next->set_mappings(entries);
        selected_slot = id;
        focused_row = row_id(id, true);
        stage(next);
        return;
    }
}

void InputMapWindow::remove_binding(StringName slot) {
    Ref<InputActionMapping> owner;
    const auto binding = find_binding(slot, &owner);
    if (binding.is_null()) {
        return;
    }
    cancel_listening();
    auto next = detached(draft);
    auto entries = next->get_mappings();
    for (int64_t i = 0; i < entries.size(); ++i) {
        if (Ref<InputActionMapping>(entries[i]) != owner) {
            continue;
        }
        auto replacement = detached(owner);
        auto slots = replacement->get_bindings();
        const int64_t index = slots.find(binding);
        slots.remove_at(index);
        replacement->set_bindings(slots);
        entries[i] = replacement;
        next->set_mappings(entries);
        if (selected_slot == slot) {
            const Ref<InputBinding> sibling = slots.is_empty() ? Ref<InputBinding>() :
                    Ref<InputBinding>(slots[std::min<int64_t>(index, slots.size() - 1)]);
            selected_slot = sibling.is_valid() ? sibling->get_id() : StringName();
            focused_row = sibling.is_valid() ? row_id(selected_slot, true) : row_id(owner->get_id(), false);
        }
        stage(next);
        return;
    }
}

void InputMapWindow::prompt_removal(Removal operation, StringName id, const String &name) {
    cancel_listening();
    pending_removal = operation;
    removal_id = id;
    const char *description = operation == Removal::ACTION ? "This action and its bindings will be removed from the map." :
            operation == Removal::BINDING ? "This binding will be removed from the map." :
            "This modifier will be removed from the binding.";
    remove_dialog->open(String("Remove \"") + name + "\"?", description);
}

void InputMapWindow::prompt_binding_removal(StringName slot) {
    const auto binding = find_binding(slot);
    if (binding.is_valid()) {
        prompt_removal(Removal::BINDING, slot, binding_label(binding));
    }
}

void InputMapWindow::prompt_modifier_removal(int64_t index) {
    const auto binding = find_binding(selected_slot);
    if (binding.is_null()) {
        return;
    }
    const auto transforms = binding->get_modifiers();
    if (index < 0 || index >= transforms.size()) {
        return;
    }
    removal_index = index;
    removal_modifier = transforms[index];
    prompt_removal(Removal::MODIFIER, selected_slot, modifier_title(removal_modifier));
}

void InputMapWindow::confirm_removal() {
    if (pending_removal == Removal::ACTION) {
        const auto mapping = find_mapping(removal_id);
        if (mapping.is_valid()) {
            const auto next = detached(draft);
            auto entries = next->get_mappings();
            entries.erase(mapping);
            next->set_mappings(entries);
            stage(next);
        }
    } else if (pending_removal == Removal::BINDING) {
        remove_binding(removal_id);
    } else if (pending_removal == Removal::MODIFIER) {
        const auto binding = find_binding(removal_id);
        if (binding.is_valid()) {
            const auto transforms = binding->get_modifiers();
            if (removal_index >= 0 && removal_index < transforms.size() &&
                    Ref<InputModifier>(transforms[removal_index]) == removal_modifier) {
                remove_modifier(removal_id, removal_index);
            }
        }
    }
    cancel_removal();
}

void InputMapWindow::cancel_removal() {
    pending_removal = Removal::NONE;
    removal_id = StringName();
    removal_index = -1;
    removal_modifier.unref();
}

void InputMapWindow::start_listening(StringName slot, Button *device_button) {
    if (listening_slot == slot) {
        cancel_listening();
        return;
    }
    cancel_listening();
    Ref<InputActionMapping> owner;
    const auto binding = find_binding(slot, &owner);
    if (binding.is_null()) {
        return;
    }
    selected_slot = slot;
    bindings->set_selected(row_id(slot, true), false);
    listening_slot = slot;
    listen_button = ObjectID(device_button->get_instance_id());
    listen_icon = device_icon(binding);
    device_button->set_button_icon(ui::icon(device_button, listen_icon, true));
    status->set_text("Press a key, mouse button, or gamepad control. Esc cancels.");
    if (owner.is_valid() && owner->get_action().is_valid() && owner->get_action()->get_kind() == InputAction::MOTION) {
        status->set_text("Move the mouse to bind relative motion. Esc cancels.");
    }
    queue_modifier_refresh();
}

void InputMapWindow::cancel_listening() {
    if (listening_slot.is_empty()) {
        return;
    }
    listening_slot = StringName();
    if (auto *button = Object::cast_to<Button>(ObjectDB::get_instance(listen_button))) {
        button->set_button_icon(ui::icon(button, listen_icon));
    }
    listen_button = ObjectID();
    listen_icon = StringName();
    status->set_text("");
}

void InputMapWindow::_input(const Ref<InputEvent> &event) {
    if (!is_visible() || event.is_null() || active_modal()) {
        return;
    }
    if (Ref<InputEventMouseMotion> motion = event; motion.is_valid() && gui_is_dragging()) {
        binding_drag_rail->hide();
        modifier_drag_rail->hide();
    }
    const Ref<InputEventKey> key = event;
    if (!listening_slot.is_empty()) {
        if (key.is_valid() && key->is_pressed() && key->get_keycode() == KEY_ESCAPE) {
            set_input_as_handled();
            cancel_listening();
            return;
        }
        const Ref<InputEventMouseButton> mouse = event;
        if (mouse.is_valid() && mouse->is_pressed() && mouse->get_button_index() == MOUSE_BUTTON_LEFT) {
            const auto *device = Object::cast_to<Button>(ObjectDB::get_instance(listen_button));
            if (device && device->get_global_rect().has_point(mouse->get_position())) {
                set_input_as_handled();
                cancel_listening();
                return;
            }
        }
        Ref<InputActionMapping> owner;
        const auto binding = find_binding(listening_slot, &owner);
        if (binding.is_valid() && owner->get_action().is_valid()) {
            const auto kind = owner->get_action()->get_kind();
            // Preserve a signed axis when editing an existing directional slot.
            const auto stick = Ref<InputEventJoypadMotion>(binding->get_event()).is_valid() ?
                    InputBinding::NONE : full_stick(event, kind);
            const auto normalized = stick == InputBinding::NONE ? normalize_capture(event, kind) : Ref<InputEvent>();
            if (stick != InputBinding::NONE || normalized.is_valid()) {
                const StringName slot = listening_slot;
                set_input_as_handled();
                cancel_listening();
                picker_focus_slot = slot;
                set_binding_source(slot, normalized, stick, binding->get_mouse_axes());
            }
        }
        return;
    }
    if (key.is_null() || !key->is_pressed() || key->is_echo() || (!key->is_ctrl_pressed() && !key->is_meta_pressed())) {
        return;
    }
    switch (key->get_keycode()) {
        case KEY_S: save(); break;
        case KEY_Z:
            if (key->is_shift_pressed()) {
                redo();
            } else {
                undo();
            }
            break;
        case KEY_Y: redo(); break;
        default: return;
    }
    set_input_as_handled();
}

void InputMapWindow::draw_modifiers() {
    modifier_refresh_pending = false;
    const int old_scroll = displayed_binding == selected_slot && modifier_scroll != nullptr
            ? modifier_scroll->get_v_scroll() : 0;
    displayed_binding = selected_slot;
    modifier_scroll = nullptr;
    while (modifiers->get_child_count() > 0) {
        auto *child = modifiers->get_child(0);
        modifiers->remove_child(child);
        child->queue_free();
    }
    Ref<InputActionMapping> owner;
    const auto binding = find_binding(selected_slot, &owner);
    auto *context = add<VBoxContainer>(inset(modifiers, spacing::lg, spacing::md, spacing::md));
    context->add_theme_constant_override("separation", ui::pixels(this, spacing::xs));
    const String context_title = binding.is_valid() ? action_title(owner) : String("Binding");
    auto *context_name = ui::label(context, context_title, ui::LabelStyle::TITLE);
    clip(context_name);
    ui::tooltip(context_name, context_title);
    context_name->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    if (binding.is_valid()) {
        auto *caption = add<HBoxContainer>(context);
        caption->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
        auto *device = add<TextureRect>(caption);
        device->set_texture(ui::icon(device, device_icon(binding)));
        device->set_modulate(ui::color(ui::Surface::MUTED));
        device->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
        device->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        auto *source = ui::label(caption, binding_label(binding), ui::LabelStyle::MUTED);
        source->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        clip(source);
        ui::tooltip(source, source->get_text());
        source->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    }
    rule(modifiers);
    if (binding.is_null() || owner->get_action().is_null()) {
        ui::empty_state(inset(modifiers, spacing::lg, spacing::lg, spacing::lg), "No binding selected",
                "Choose a binding from the action list to edit its modifiers.");
        return;
    }
    auto *title_row = add<HBoxContainer>(inset(modifiers, spacing::lg, spacing::md, spacing::xs));
    title_row->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *heading_label = ui::label(title_row, "Modifiers", ui::LabelStyle::HEADING);
    heading_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    const auto kind = owner->get_action()->get_kind();
    auto *add_transform = ui::picker(title_row, "Add modifier", callable_mp(this, &InputMapWindow::add_modifier));
    add_transform->set_name("AddBindingModifier");
    add_transform->set_h_size_flags(Control::SIZE_SHRINK_END);
    auto *add_trigger = add_transform->get_trigger();
    add_trigger->set_theme_type_variation("CrimsonQuiet");
    // Let Godot account for both the fixed caption and the trailing chevron.
    add_trigger->set_clip_text(false);
    add_trigger->set_custom_minimum_size(Vector2(0, ui::pixels(this, 28)));
    modifier_scroll = add<ScrollContainer>(modifiers);
    modifier_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    modifier_scroll->set_follow_focus(true);
    modifier_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    auto *stack_inset = inset(modifier_scroll, spacing::lg, spacing::xs, spacing::lg);
    stack_inset->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    auto *stack = ui::field_group(stack_inset);
    stack->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    if (has_legacy_contribution(*binding.ptr(), kind)) {
        const auto value = binding->get_contribution();
        auto *legacy = ui::field_group(stack, "Legacy contribution");
        auto *contribution = ui::label(legacy, "X " + String::num(value.x) + String("  /  Y ") + String::num(value.y),
                ui::LabelStyle::MUTED);
        contribution->set_tooltip_text("This older binding applies these values before the modifier stack. Its behavior is preserved.");
        contribution->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        if (can_convert_contribution(*binding.ptr(), kind)) {
            ui::button(legacy, "Convert to modifiers", "Represent this contribution with equivalent modifiers.",
                    callable_mp(this, &InputMapWindow::convert_contribution), ui::ButtonStyle::QUIET);
        }
    } else if (kind == InputAction::VECTOR && binding->get_stick() == InputBinding::NONE) {
        auto *hint = ui::label(stack, "A single control starts on X. Swap Axis moves it to Y.", ui::LabelStyle::MUTED);
        hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    } else if (kind == InputAction::MOTION) {
        auto *hint = ui::label(stack, "Relative mouse movement is measured in pixels.", ui::LabelStyle::MUTED);
        hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    }
    const auto transforms = binding->get_modifiers();
    if (transforms.is_empty()) {
        auto *empty = ui::label(stack, kind == InputAction::BUTTON
                ? "Button bindings use their pressed state directly." : "No modifiers applied.", ui::LabelStyle::MUTED);
        empty->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    }
    for (int64_t i = 0; i < transforms.size(); ++i) {
        const Ref<InputModifier> modifier = transforms[i];
        const Ref<InputDeadZone> dead_zone = modifier;
        const Ref<InputSwapAxis> swap_axis = modifier;
        const Ref<InputInvert> invert = modifier;
        const Ref<InputScale> scale = modifier;
        const Ref<InputResponseCurve> response_curve = modifier;
        const Ref<InputNormalize> normalize = modifier;
        auto *heading = memnew(HBoxContainer);
        heading->add_theme_constant_override("separation", ui::pixels(this, spacing::xs));
        auto *modifier_name = ui::label(heading, modifier_title(modifier), ui::LabelStyle::HEADING);
        modifier_name->set_name("ModifierDragHandle");
        modifier_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        clip(modifier_name);
        ui::tooltip(modifier_name, modifier_name->get_text());
        modifier_name->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        if (transforms.size() > 1) {
            modifier_name->set_default_cursor_shape(Control::CURSOR_DRAG);
            modifier_name->set_accessibility_description("Drag this modifier before or after another card. Use the order buttons with a keyboard.");
            ui::tooltip(modifier_name, modifier_name->get_text() + String(". Drag the title to reorder."));
            modifier_name->set_drag_forwarding(
                    callable_mp(this, &InputMapWindow::begin_modifier_drag).bind(modifier_name, selected_slot, i, binding),
                    callable_mp(this, &InputMapWindow::can_drop_modifier).bind(selected_slot, i, modifier_name),
                    callable_mp(this, &InputMapWindow::drop_modifier).bind(selected_slot, i, modifier_name));
            auto *up = ui::icon_button(heading, "chevron_up", "Move this modifier earlier in the stack.",
                    callable_mp(this, &InputMapWindow::move_modifier).bind(i, -1), ui::ButtonStyle::QUIET);
            auto *down = ui::icon_button(heading, "chevron_down", "Move this modifier later in the stack.",
                    callable_mp(this, &InputMapWindow::move_modifier).bind(i, 1), ui::ButtonStyle::QUIET);
            up->set_disabled(i == 0);
            down->set_disabled(i + 1 == transforms.size());
        }
        ui::icon_button(heading, "remove", "Remove this modifier.",
                callable_mp(this, &InputMapWindow::prompt_modifier_removal).bind(i), ui::ButtonStyle::QUIET);
        auto *fields = memnew(VBoxContainer);
        fields->add_theme_constant_override("separation", ui::pixels(this, spacing::xs));
        if (dead_zone.is_valid()) {
            auto *mode = ui::select(nullptr, {"Radial", "Axial"}, dead_zone->get_mode(),
                    callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(Setting::DEAD_ZONE_MODE)));
            mode->set_name("DeadZoneMode");
            const String mode_help = "Radial compares vector length. Axial tests each component independently.";
            ui::field(fields, "Mode", mode, mode_help);
            ui::tooltip(mode->get_trigger(), mode_help);
            compact_field_value(mode, 84);
            auto *lower = ui::number_input(nullptr, "Lower threshold", dead_zone->get_lower_threshold(), 0, 0.99, 0.01,
                    callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(Setting::DEAD_ZONE_LOWER)),
                    ui::FieldState::EDITABLE, "Magnitude at or below this value produces zero.");
            lower->set_name(String("LowerThreshold") + String::num_int64(i));
            ui::field(fields, "Lower threshold", lower, "Input magnitude from 0 to 1 where output starts increasing.");
            auto *upper = ui::number_input(nullptr, "Upper threshold", dead_zone->get_upper_threshold(), 0.01, 1, 0.01,
                    callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(Setting::DEAD_ZONE_UPPER)),
                    ui::FieldState::EDITABLE, "Magnitude at or above this value produces full output.");
            upper->set_name(String("UpperThreshold") + String::num_int64(i));
            ui::field(fields, "Upper threshold", upper, "Input magnitude from 0 to 1 where output reaches full scale.");
        } else if (swap_axis.is_valid()) {
            const bool swapped = swap_axis->get_order() == InputSwapAxis::YX;
            auto *x = ui::select(nullptr, {"X", "Y"}, swapped ? 1 : 0,
                    callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(Setting::OUTPUT_X)));
            auto *y = ui::select(nullptr, {"X", "Y"}, swapped ? 0 : 1,
                    callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(Setting::OUTPUT_Y)));
            const String x_help = "Choose the source axis for horizontal output.";
            const String y_help = "Choose the source axis for vertical output.";
            ui::field(fields, "Output X from", x, x_help);
            ui::field(fields, "Output Y from", y, y_help);
            ui::tooltip(x->get_trigger(), x_help);
            ui::tooltip(y->get_trigger(), y_help);
            compact_field_value(x, 62);
            compact_field_value(y, 62);
        } else if (invert.is_valid()) {
            const auto create_axis = [&](const char *name, bool active, Setting setting, const char *help) {
                const auto checked = active ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED;
                auto *toggle = ui::checkbox(nullptr, "", checked,
                        callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(setting)));
                ui::field(fields, name, toggle, help);
                toggle->set_h_size_flags(Control::SIZE_SHRINK_END);
            };
            create_axis("Invert X", invert->get_x(), Setting::INVERT_X, "Reverse the sign of the X component.");
            if (kind != InputAction::AXIS) {
                create_axis("Invert Y", invert->get_y(), Setting::INVERT_Y, "Reverse the sign of the Y component.");
            }
        } else if (scale.is_valid()) {
            const auto create_axis = [&](const char *name, double factor, Setting setting, const char *help) {
                auto *number = ui::number_input(nullptr, name, factor, std::min(-100.0, factor), std::max(100.0, factor), 0.05,
                        callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(setting)),
                        ui::FieldState::EDITABLE, help);
                ui::field(fields, name, number, help);
            };
            create_axis("X factor", scale->get_factor().x, Setting::SCALE_X, "Multiply the X component by this factor.");
            if (kind != InputAction::AXIS) {
                create_axis("Y factor", scale->get_factor().y, Setting::SCALE_Y, "Multiply the Y component by this factor.");
            }
        } else if (response_curve.is_valid()) {
            const char *help = "One is linear. Above one reduces low input. Below one increases low input.";
            auto *number = ui::number_input(nullptr, "Exponent", response_curve->get_exponent(), 0.01, 16, 0.05,
                    callable_mp(this, &InputMapWindow::change_setting).bind(i, static_cast<int64_t>(Setting::RESPONSE_EXPONENT)),
                    ui::FieldState::EDITABLE, help);
            ui::field(fields, "Exponent", number, help);
        } else if (normalize.is_valid()) {
            ui::label(fields, "Keep the direction and set nonzero length to one.", ui::LabelStyle::MUTED)
                    ->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        }
        auto *card = ui::card(stack, heading, fields, nullptr, ui::color(ui::Surface::HEADER));
        if (transforms.size() > 1) {
            card->set_meta("input_modifier_drop_card", true);
            card->set_drag_forwarding({}, callable_mp(this, &InputMapWindow::can_drop_modifier).bind(selected_slot, i, card),
                    callable_mp(this, &InputMapWindow::drop_modifier).bind(selected_slot, i, card));
        }
    }

    std::vector<ui::PickerOption> options;
    if (kind == InputAction::AXIS || kind == InputAction::VECTOR) {
        options.push_back({"dead_zone", "Dead Zone", "", nullptr});
    }
    if (kind == InputAction::VECTOR || kind == InputAction::MOTION) {
        options.push_back({"swap_axis", "Swap Axis", "", nullptr});
    }
    if (kind != InputAction::BUTTON) {
        options.push_back({"invert", "Invert", "", nullptr});
        options.push_back({"scale", "Scale", "", nullptr});
    }
    if (kind == InputAction::AXIS || kind == InputAction::VECTOR) {
        options.push_back({"response_curve", "Response Curve", "", nullptr});
    }
    if (kind == InputAction::VECTOR) {
        options.push_back({"normalize", "Normalize", "", nullptr});
    }
    const bool supports_modifiers = !options.empty();
    add_transform->set_options(std::move(options));
    add_transform->set_state(supports_modifiers ? ui::FieldState::EDITABLE : ui::FieldState::DISABLED);
    add_transform->get_trigger()->set_tooltip_text(supports_modifiers ? "Add an ordered transformation to this binding." :
            "Button actions do not have analog modifiers.");
    modifier_scroll->set_deferred("scroll_vertical", old_scroll);
}

Ref<InputActionMap> InputMapWindow::replace_binding(const Ref<InputBinding> &binding, const Ref<InputBinding> &replacement) const {
    Ref<InputActionMapping> owner;
    find_binding(binding->get_id(), &owner);
    if (owner.is_null()) {
        return {};
    }
    const auto map = detached(draft);
    auto entries = map->get_mappings();
    const auto mapping = detached(owner);
    auto slots = mapping->get_bindings();
    slots[slots.find(binding)] = replacement;
    mapping->set_bindings(slots);
    entries[entries.find(owner)] = mapping;
    map->set_mappings(entries);
    return map;
}

void InputMapWindow::add_modifier(const StringName &type) {
    const auto binding = find_binding(selected_slot);
    if (binding.is_null()) {
        return;
    }
    Ref<InputModifier> modifier;
    if (type == StringName("dead_zone")) {
        Ref<InputDeadZone> dead_zone;
        dead_zone.instantiate();
        modifier = dead_zone;
    } else if (type == StringName("swap_axis")) {
        Ref<InputSwapAxis> swap_axis;
        swap_axis.instantiate();
        modifier = swap_axis;
    } else if (type == StringName("invert")) {
        Ref<InputInvert> invert;
        invert.instantiate();
        modifier = invert;
    } else if (type == StringName("scale")) {
        Ref<InputScale> scale;
        scale.instantiate();
        modifier = scale;
    } else if (type == StringName("normalize")) {
        Ref<InputNormalize> normalize;
        normalize.instantiate();
        modifier = normalize;
    } else if (type == StringName("response_curve")) {
        Ref<InputResponseCurve> curve;
        curve.instantiate();
        modifier = curve;
    }
    if (modifier.is_valid()) {
        auto replacement = detached(binding);
        auto transforms = replacement->get_modifiers();
        transforms.append(modifier);
        replacement->set_modifiers(transforms);
        stage(replace_binding(binding, replacement), false);
    }
}

void InputMapWindow::change_setting(double value, int64_t index, int64_t setting_id) {
    const auto binding = find_binding(selected_slot);
    if (binding.is_null()) {
        return;
    }
    const auto setting = static_cast<Setting>(setting_id);
    const auto replacement = detached(binding);
    auto transforms = replacement->get_modifiers();
    if (index < 0 || index >= transforms.size()) {
        return;
    }
    const auto modifier = detached(Ref<InputModifier>(transforms[index]));
    const Ref<InputDeadZone> dead_zone = modifier;
    const Ref<InputSwapAxis> swap_axis = modifier;
    const Ref<InputInvert> invert = modifier;
    const Ref<InputScale> scale = modifier;
    const Ref<InputResponseCurve> curve = modifier;
    if (dead_zone.is_valid()) {
        const double previous_lower = dead_zone->get_lower_threshold();
        const double previous_upper = dead_zone->get_upper_threshold();
        if (setting == Setting::DEAD_ZONE_MODE) {
            dead_zone->set_mode(static_cast<InputDeadZone::Mode>(static_cast<int>(value)));
        } else if (setting == Setting::DEAD_ZONE_LOWER) {
            dead_zone->set_lower_threshold(value);
        } else if (setting == Setting::DEAD_ZONE_UPPER) {
            dead_zone->set_upper_threshold(value);
        }
        if (!(dead_zone->get_lower_threshold() < dead_zone->get_upper_threshold())) {
            status->set_text("Lower threshold must be below upper threshold.");
            const bool lower = setting == Setting::DEAD_ZONE_LOWER;
            const auto name = String(lower ? "LowerThreshold" : "UpperThreshold") + String::num_int64(index);
            if (auto *field = Object::cast_to<ui::NumberInput>(modifiers->find_child(name, true, false))) {
                field->set_value(lower ? previous_lower : previous_upper);
            }
            return;
        }
    } else if (swap_axis.is_valid()) {
        const bool swapped = setting == Setting::OUTPUT_X ? value == 1 : value == 0;
        swap_axis->set_order(swapped ? InputSwapAxis::YX : InputSwapAxis::XY);
    } else if (invert.is_valid()) {
        if (setting == Setting::INVERT_X) {
            invert->set_x(value != 0);
        } else if (setting == Setting::INVERT_Y) {
            invert->set_y(value != 0);
        }
    } else if (scale.is_valid()) {
        auto factor = scale->get_factor();
        if (setting == Setting::SCALE_X) {
            factor.x = value;
        } else if (setting == Setting::SCALE_Y) {
            factor.y = value;
        }
        scale->set_factor(factor);
    } else if (curve.is_valid() && setting == Setting::RESPONSE_EXPONENT) {
        curve->set_exponent(value);
    } else {
        return;
    }
    transforms[index] = modifier;
    replacement->set_modifiers(transforms);
    stage(replace_binding(binding, replacement), false, swap_axis.is_valid());
}

void InputMapWindow::convert_contribution() {
    Ref<InputActionMapping> owner;
    const auto binding = find_binding(selected_slot, &owner);
    if (binding.is_null() || owner->get_action().is_null()) {
        return;
    }
    const auto kind = owner->get_action()->get_kind();
    if (!can_convert_contribution(*binding.ptr(), kind)) {
        return;
    }
    const auto value = binding->get_contribution();
    const auto replacement = detached(binding);
    TypedArray<InputModifier> converted;
    if (kind == InputAction::MOTION) {
        Ref<InputScale> scale;
        scale.instantiate();
        scale->set_factor(Vector2(value.x, value.y));
        converted.append(scale);
        replacement->set_contribution(Vector3(1, 1, 0));
    } else {
        const bool to_y = value.y != 0;
        if (to_y) {
            Ref<InputSwapAxis> swap;
            swap.instantiate();
            converted.append(swap);
        }
        const real_t factor = to_y ? value.y : value.x;
        if (factor == -1) {
            Ref<InputInvert> invert;
            invert.instantiate();
            invert->set_x(!to_y);
            invert->set_y(to_y);
            converted.append(invert);
        } else if (factor != 1) {
            Ref<InputScale> scale;
            scale.instantiate();
            scale->set_factor(to_y ? Vector2(1, factor) : Vector2(factor, 1));
            converted.append(scale);
        }
        replacement->set_contribution(Vector3(1, 0, 0));
    }
    for (const Variant &modifier : binding->get_modifiers()) {
        converted.append(modifier);
    }
    replacement->set_modifiers(converted);
    stage(replace_binding(binding, replacement), false);
}

void InputMapWindow::move_modifier(int64_t index, int64_t direction) {
    const auto binding = find_binding(selected_slot);
    if (binding.is_null()) {
        return;
    }
    auto transforms = binding->get_modifiers();
    const int64_t adjacent = index + direction;
    if (index < 0 || index >= transforms.size() || adjacent < 0 || adjacent >= transforms.size() || adjacent == index) {
        return;
    }
    const Variant held = transforms[index];
    transforms.remove_at(index);
    transforms.insert(adjacent, held);
    const auto replacement = detached(binding);
    replacement->set_modifiers(transforms);
    stage(replace_binding(binding, replacement), false);
}

int64_t InputMapWindow::modifier_drag_source(const Variant &data) const {
    if (data.get_type() != Variant::DICTIONARY) {
        return -1;
    }
    const Dictionary payload = data;
    if (StringName(payload.get("kind", StringName())) != StringName("crimson_input_modifier") ||
            StringName(payload.get("slot", StringName())) != selected_slot) {
        return -1;
    }
    const Ref<InputBinding> snapshot = payload.get("binding", Variant());
    if (snapshot.is_null() || find_binding(selected_slot) != snapshot) {
        return -1;
    }
    return payload.get("index", int64_t(-1));
}

Variant InputMapWindow::begin_modifier_drag(const Vector2 &, Control *handle, StringName slot, int64_t index,
        const Ref<InputBinding> &binding) {
    Dictionary data;
    data["kind"] = StringName("crimson_input_modifier");
    data["slot"] = slot;
    data["index"] = index;
    data["binding"] = binding;
    if (modifier_drag_source(data) < 0) {
        return {};
    }
    ui::show_drag_preview(handle, modifier_title(Ref<InputModifier>(binding->get_modifiers()[index])));
    return data;
}

bool InputMapWindow::can_drop_modifier(const Vector2 &point, const Variant &data, StringName slot, int64_t index,
        Control *target) const {
    const int64_t source = modifier_drag_source(data);
    const bool valid = slot == selected_slot && source >= 0 && source != index;
    if (valid && point.is_finite()) {
        for (Node *owner = target; owner; owner = owner->get_parent()) {
            if (owner->has_meta("input_modifier_drop_card")) {
                const auto *card = Object::cast_to<Control>(owner);
                ui::place_drag_insertion_rail(modifier_drag_rail, card->get_global_rect(), modifier_scroll->get_global_rect(),
                        point.y >= target->get_size().y * 0.5);
                break;
            }
        }
    }
    return valid;
}

void InputMapWindow::drop_modifier(const Vector2 &point, const Variant &data, StringName slot, int64_t index,
        Control *target) {
    if (!can_drop_modifier(point, data, slot, index, target)) {
        return;
    }
    const int64_t source = modifier_drag_source(data);
    const int64_t boundary = index + int64_t(point.y >= target->get_size().y * 0.5);
    const int64_t destination = boundary - int64_t(source < boundary);
    move_modifier(source, destination - source);
}

void InputMapWindow::remove_modifier(StringName slot, int64_t index) {
    const auto binding = find_binding(slot);
    if (binding.is_null()) {
        return;
    }
    auto transforms = binding->get_modifiers();
    if (index >= 0 && index < transforms.size()) {
        transforms.remove_at(index);
        const auto replacement = detached(binding);
        replacement->set_modifiers(transforms);
        stage(replace_binding(binding, replacement), false);
    }
}

void InputMapWindow::map_command(int64_t command) {
    switch (command) {
        case NEW_MAP: new_map(); break;
        case OPEN_MAP: choose_map_asset(); break;
        case OPEN_PRESET: open_preset(); break;
        case MAP_SETTINGS: edit_metadata(); break;
        case SAVE_COPY: save_copy(); break;
        case UNDO_DRAFT: undo(); break;
        case REDO_DRAFT: redo(); break;
    }
}

void InputMapWindow::action_command(int64_t command, StringName mapping_id) {
    if (command == 0) {
        choose_action_asset();
        return;
    }
    if (command == 1) {
        prompt_action();
        return;
    }
    const auto mapping = find_mapping(mapping_id);
    if (mapping.is_null()) {
        return;
    }
    if (command == 2) {
        edit_metadata(mapping_id);
    } else if (command == 3 && mapping->get_action().is_valid()) {
        EditorInterface::get_singleton()->edit_resource(mapping->get_action());
    } else if (command == 4) {
        prompt_removal(Removal::ACTION, mapping_id, action_title(mapping));
    }
}

void InputMapWindow::edit_metadata(StringName mapping_id) {
    metadata_mapping = mapping_id;
    if (mapping_id.is_empty()) {
        Object::cast_to<Control>(metadata_title->get_parent())->show();
        metadata_title->set_text(draft->get_display_name());
        metadata_id->set_text(draft->get_id());
    } else {
        Object::cast_to<Control>(metadata_title->get_parent())->hide();
        metadata_id->set_text(mapping_id);
    }
    metadata_dialog->open(mapping_id.is_empty() ? "Map Settings" : "Edit Action ID", {}, metadata_id);
    metadata_id->select_all();
}

void InputMapWindow::apply_metadata() {
    const StringName id(metadata_id->get_text().strip_edges());
    const auto next = detached(draft);
    if (metadata_mapping.is_empty()) {
        next->set_id(id);
        next->set_display_name(metadata_title->get_text().strip_edges());
        stage(next);
    } else if (id != metadata_mapping) {
        const auto original = find_mapping(metadata_mapping);
        if (original.is_null()) {
            return;
        }
        auto entries = next->get_mappings();
        const auto replacement = detached(original);
        replacement->set_id(id);
        entries[entries.find(original)] = replacement;
        next->set_mappings(entries);
        stage(next);
    }
}

bool InputMapWindow::validate_metadata() {
    const StringName id(metadata_id->get_text().strip_edges());
    String problem;
    if (id.is_empty()) {
        problem = "Enter an ID before applying these settings.";
    } else if (!metadata_mapping.is_empty() && id != metadata_mapping && find_mapping(id).is_valid()) {
        problem = "Another action already uses this ID.";
    }
    metadata_dialog->set_validation_message(problem);
    if (!problem.is_empty()) {
        metadata_id->grab_focus();
    }
    return problem.is_empty();
}

void InputMapWindow::choose_action_asset() {
    cancel_listening();
    chosen_action.unref();
    action_query->clear();
    action_picker->open("Choose Action Asset", {}, action_query, 540);
    read_action_assets();
}

void InputMapWindow::read_action_assets() {
    if (!action_picker->is_open()) {
        return;
    }
    action_paths.clear();
    auto *filesystem = EditorInterface::get_singleton()->get_resource_filesystem();
    scanning_actions = filesystem && filesystem->is_scanning();
    if (filesystem && !scanning_actions) {
        collect_action_paths(filesystem->get_filesystem(), action_paths);
        action_paths.sort();
    }
    filter_action_assets(action_query->get_text());
}

void InputMapWindow::finish_action_scan(bool) {
    if (scanning_actions) {
        read_action_assets();
    }
}

void InputMapWindow::filter_action_assets(const String &query) {
    if (!action_picker->is_open()) {
        return;
    }
    action_picker->set_validation_message({});
    const auto *previous = action_results->get_selected();
    const String preserved = previous ? String(previous->get_metadata(0)) : String();
    const bool restore_result_focus = action_results->has_focus();
    action_results->clear();
    TreeItem *root = action_results->create_item();
    TreeItem *selected = nullptr;
    const String term = query.strip_edges();
    const Ref<Texture2D> file_icon = ui::icon(action_results, "file");
    for (const String &path : action_paths) {
        const String stem = path.get_file().get_basename();
        const String title = stem.capitalize();
        if (!term.is_empty() && title.findn(term) < 0 && path.findn(term) < 0) {
            continue;
        }
        auto *result = action_results->create_item(root);
        result->set_text(0, title);
        result->set_icon(0, file_icon);
        result->set_metadata(0, path);
        result->set_tooltip_text(0, path);
        const String folder = path.get_base_dir().trim_prefix("res://");
        result->set_text(1, folder.is_empty() ? String("Project root") : folder);
        result->set_custom_color(1, ui::color(ui::Surface::MUTED));
        result->set_tooltip_text(1, path);
        if (path == preserved) {
            selected = result;
        }
    }
    if (action_empty) {
        action_picker->body()->remove_child(action_empty);
        action_empty->queue_free();
        action_empty = nullptr;
    }
    const bool has_matches = root->get_first_child() != nullptr;
    action_results->set_visible(has_matches);
    action_picker->set_confirm_enabled(has_matches);
    if (has_matches) {
        action_results->set_selected(selected ? selected : root->get_first_child(), 0);
        action_results->ensure_cursor_is_visible();
    } else {
        if (restore_result_focus) {
            action_query->edit();
        }
        if (scanning_actions) {
            action_empty = ui::empty_state(action_picker->body(), "Indexing project assets", "Results appear when Godot finishes scanning.");
        } else if (action_paths.is_empty()) {
            action_empty = ui::empty_state(action_picker->body(), "No InputAction assets", "Use New Action Asset to create an action definition.");
        } else {
            action_empty = ui::empty_state(action_picker->body(), "No matching actions", "Try a different asset name or folder.");
        }
    }
}

void InputMapWindow::navigate_action_results(const Ref<InputEvent> &event) {
    if (action_results->is_visible() &&
            (event->is_action_pressed("ui_down") || event->is_action_pressed("ui_up"))) {
        if (event->is_action_pressed("ui_up")) {
            auto *root = action_results->get_root();
            action_results->set_selected(root->get_child(root->get_child_count() - 1), 0);
            action_results->ensure_cursor_is_visible();
        }
        action_query->accept_event();
        action_results->grab_focus();
    }
}

bool InputMapWindow::validate_action_choice() {
    chosen_action.unref();
    if (const auto *selected = action_results->get_selected()) {
        const String path = selected->get_metadata(0);
        chosen_action = ResourceLoader::get_singleton()->load(path, "InputAction");
    }
    if (chosen_action.is_null()) {
        action_picker->set_validation_message("This action could not be opened. Select another InputAction asset.");
    }
    return chosen_action.is_valid();
}

void InputMapWindow::confirm_action_choice() {
    add_action(chosen_action);
    chosen_action.unref();
}

void InputMapWindow::prompt_action() {
    cancel_listening();
    action_name->clear();
    action_kind->select(0);
    action_dialog->open("New Input Action", {}, action_name);
}

void InputMapWindow::save_new_action() {
    const auto name = action_name->get_text().strip_edges();
    new_action.instantiate();
    new_action->set_display_name(name);
    new_action->set_kind(static_cast<InputAction::ValueKind>(action_kind->get_selected_index()));
    new_action->set_requires_pointer(new_action->get_kind() == InputAction::MOTION);
    pick_save_path(FilePurpose::SAVE_ACTION, name.to_snake_case() + ".tres");
}

bool InputMapWindow::validate_action_name() {
    const bool valid = !action_name->get_text().strip_edges().is_empty();
    action_dialog->set_validation_message(valid ? String() : String("Enter a display name for the new action."));
    if (!valid) {
        action_name->grab_focus();
    }
    return valid;
}

void InputMapWindow::add_action(const Ref<InputAction> &action) {
    const auto next = detached(draft);
    const String title = action->get_display_name().strip_edges();
    const String proposed = title.is_empty() ? action->get_path().get_file().get_basename().strip_edges() : title;
    const String base = proposed.is_empty() ? String("action") : proposed.to_snake_case().replace("/", "_");
    StringName id(base);
    for (int suffix = 2; find_mapping(id).is_valid(); ++suffix) {
        id = StringName(base + String("_") + String::num_int64(suffix));
    }
    next->add_action(id, action);
    focused_row = row_id(id, false);
    stage(next);
}

void InputMapWindow::choose_map_asset() {
    cancel_listening();
    TypedArray<StringName> types;
    types.append(StringName("InputActionMap"));
    EditorInterface::get_singleton()->popup_quick_open(callable_mp(this, &InputMapWindow::map_chosen), types);
}

void InputMapWindow::map_chosen(const String &path) {
    if (!path.is_empty()) {
        const Ref<InputActionMap> map = ResourceLoader::get_singleton()->load(path, "InputActionMap");
        if (map.is_valid()) {
            open(map, assignment_layout, ObjectDB::get_instance(assignment_owner));
            if (!discard_dialog->is_open()) {
                assign_first_map(map);
            }
        } else {
            status->set_text("Choose an InputActionMap resource.");
        }
    }
}

void InputMapWindow::open_preset() {
    map_chosen(fps_preset);
}

void InputMapWindow::new_map() {
    cancel_listening();
    if (draft != saved) {
        next_source.unref();
        next_layout = assignment_layout;
        next_owner = assignment_owner;
        discard_dialog->open("Discard map draft?", "This map has unsaved edits.");
    } else {
        load_map({}, assignment_layout, assignment_owner);
    }
}

void InputMapWindow::undo() {
    cancel_listening();
    if (revision > 0) {
        draft = history[--revision];
        queue_redraw();
    }
}

void InputMapWindow::redo() {
    cancel_listening();
    if (revision + 1 < history.size()) {
        draft = history[++revision];
        queue_redraw();
    }
}

void InputMapWindow::save() {
    cancel_listening();
    if (draft->validate() != OK) {
        refresh_frame();
        return;
    }
    const String path = source.is_valid() ? source->get_path() : String();
    if (path.is_empty() || path.contains("::") || path == fps_preset) {
        pick_save_path(FilePurpose::SAVE_MAP, map_title(draft).to_snake_case().validate_filename() + ".tres");
    } else {
        save_map(path, false);
    }
}

void InputMapWindow::save_copy() {
    cancel_listening();
    if (draft->validate() != OK) {
        refresh_frame();
        return;
    }
    pick_save_path(FilePurpose::SAVE_COPY, map_title(draft).to_snake_case().validate_filename() + "_copy.tres");
}

void InputMapWindow::pick_save_path(FilePurpose purpose, const String &name) {
    file_purpose = purpose;
    file_dialog->set_current_file(name.validate_filename());
    file_dialog->popup_centered_clamped(Vector2i(ui::pixels(this, 670), ui::pixels(this, 440)));
}

void InputMapWindow::file_selected(const String &path) {
    const String destination = path.get_extension().is_empty() ? path + String(".tres") : path;
    if (ResourceLoader::get_singleton()->exists(destination)) {
        status->set_text("Choose a new resource file name.");
        return;
    }
    if (file_purpose == FilePurpose::SAVE_ACTION) {
        if (new_action.is_null()) {
            return;
        }
        const Error error = ResourceSaver::get_singleton()->save(new_action, destination);
        if (error != OK) {
            status->set_text("Could not save the action: " + UtilityFunctions::error_string(error));
            return;
        }
        new_action->take_over_path(destination);
        if (auto *filesystem = EditorInterface::get_singleton()->get_resource_filesystem()) {
            filesystem->update_file(destination);
        }
        add_action(new_action);
        new_action.unref();
        return;
    }
    save_map(destination, true);
}

void InputMapWindow::cancel_file_selection() {
    new_action.unref();
    file_purpose = FilePurpose::SAVE_MAP;
}

void InputMapWindow::save_map(const String &path, bool new_file) {
    if (!new_file && revision_of(source) != source_revision) {
        status->set_text("This map changed outside this window. Reopen it or save your draft as a copy.");
        return;
    }
    const auto output = snapshot(draft);
    if (assignment_layout.is_valid() && (!new_file || file_purpose != FilePurpose::SAVE_COPY)) {
        auto maps = assignment_layout->get_maps();
        const int64_t index = source.is_valid() ? maps.find(source) : -1;
        if (index >= 0 || (maps.is_empty() && assignment_layout->get_actions().is_empty())) {
            const auto layout = detached(assignment_layout);
            if (index >= 0) {
                maps[index] = output;
            } else {
                maps.append(output);
            }
            layout->set_maps(maps);
            if (layout->validate() != OK) {
                status->set_text("The resulting layout is invalid. Check for duplicate IDs or an invalid pointer release action.");
                return;
            }
        }
    }
    const Error error = ResourceSaver::get_singleton()->save(output, path);
    if (error != OK) {
        status->set_text("Could not save the map: " + UtilityFunctions::error_string(error));
        return;
    }
    if (!new_file && source.is_valid() && draft != saved) {
        auto *undo_redo = EditorInterface::get_singleton()->get_editor_undo_redo();
        undo_redo->create_action("Edit Input Map", UndoRedo::MERGE_DISABLE, source.ptr());
        for (const char *property : {"id", "display_name", "mappings"}) {
            undo_redo->add_do_property(source.ptr(), property, output->get(property));
            undo_redo->add_undo_property(source.ptr(), property, source->get(property));
        }
        undo_redo->commit_action();
    } else if (new_file) {
        const bool copy = file_purpose == FilePurpose::SAVE_COPY;
        source = output;
        source->take_over_path(path);
        if (!copy) {
            assign_first_map(source);
        } else {
            assignment_layout.unref();
            assignment_owner = ObjectID();
        }
    }
    source_revision = revision_of(source);
    saved = draft;
    refresh_frame();
    status->set_text("Saved " + path.get_file());
}

void InputMapWindow::assign_first_map(const Ref<InputActionMap> &map) {
    if (map.is_null() || map->validate() != OK) {
        return;
    }
    auto *owner = ObjectDB::get_instance(assignment_owner);
    auto *undo_redo = EditorInterface::get_singleton()->get_editor_undo_redo();
    if (assignment_layout.is_valid() && assignment_layout->get_maps().is_empty() &&
            assignment_layout->get_actions().is_empty()) {
        if (owner && Ref<InputLayout>(owner->get("input_layout")) != assignment_layout) {
            return;
        }
        const TypedArray<InputActionMap> original = assignment_layout->get_maps();
        TypedArray<InputActionMap> maps;
        maps.append(map);
        const auto layout = detached(assignment_layout);
        layout->set_maps(maps);
        if (layout->validate() != OK) {
            status->set_text("This map does not meet the layout's input requirements.");
            return;
        }
        undo_redo->create_action("Assign Input Map", UndoRedo::MERGE_DISABLE, assignment_layout.ptr());
        undo_redo->add_do_property(assignment_layout.ptr(), "maps", maps);
        undo_redo->add_undo_property(assignment_layout.ptr(), "maps", original);
        undo_redo->commit_action();
    } else if (assignment_layout.is_null() && owner && Ref<InputLayout>(owner->get("input_layout")).is_null()) {
        assignment_layout = InputLayout::create()->with_map(map);
        undo_redo->create_action("Assign Input Layout", UndoRedo::MERGE_DISABLE, owner);
        undo_redo->add_do_property(owner, "input_layout", assignment_layout);
        undo_redo->add_undo_property(owner, "input_layout", Ref<InputLayout>());
        undo_redo->commit_action();
    }
}

void InputMapWindow::confirm_discard() {
    const auto map = next_source;
    const auto layout = next_layout;
    const auto owner = next_owner;
    cancel_discard();
    load_map(map, layout, owner);
    if (map.is_valid()) {
        assign_first_map(map);
    }
}

void InputMapWindow::cancel_discard() {
    next_source.unref();
    next_layout.unref();
    next_owner = ObjectID();
}

void InputMapWindow::close_window() {
    if (auto *modal = active_modal()) {
        modal->cancel();
        return;
    }
    cancel_listening();
    hide();
}

ui::EditorModal *InputMapWindow::active_modal() const {
    for (auto *modal : {discard_dialog, remove_dialog, action_dialog, action_picker, metadata_dialog}) {
        if (modal && modal->is_open()) {
            return modal;
        }
    }
    return nullptr;
}

} // namespace crimson::input

#endif


