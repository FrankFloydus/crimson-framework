// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "editor_controls.hpp"

#ifdef DEBUG_ENABLED

#include "editor_spacing.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/popup_panel.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace crimson::editor_ui {
using namespace godot;

namespace {

/// Add an owned stock control to the picker.
template <typename T>
T *add(Node *owner) {
    T *control = memnew(T);
    if (owner) {
        owner->add_child(control);
    }
    return control;
}

/// Keep decorative option content out of hit testing and focus traversal.
void display_content(Control *content, Control *owner, int right_padding) {
    content->reparent(owner, false);
    content->set_mouse_behavior_recursive(Control::MOUSE_BEHAVIOR_DISABLED);
    content->set_focus_behavior_recursive(Control::FOCUS_BEHAVIOR_DISABLED);
    content->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    content->set_offset(SIDE_LEFT, pixels(owner, 8));
    content->set_offset(SIDE_TOP, 0);
    content->set_offset(SIDE_RIGHT, -pixels(owner, right_padding));
    content->set_offset(SIDE_BOTTOM, 0);
}

} // namespace

void Picker::configure(const String &prompt, const Callable &chosen, PickerMode presentation, SelectStyle style) {
    placeholder = prompt;
    on_chosen = chosen;
    mode = presentation;
    select_style = style;
    trigger = button(this, prompt, "Choose a value.", callable_mp(this, &Picker::open));
    if (mode == PickerMode::COMPACT) {
        trigger->set_name("SelectTrigger");
    }
    trigger->set_theme_type_variation("CrimsonSelect");
    trigger->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    if (trigger->is_inside_tree()) {
        trigger->set_button_icon(icon(trigger, "chevron_down"));
    } else {
        trigger->connect("ready", callable_mp_static(+[](Button *anchor) {
            anchor->set_button_icon(icon(anchor, "chevron_down"));
        }).bind(trigger), Object::CONNECT_ONE_SHOT);
    }
    trigger->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    trigger->set_clip_text(true);
    trigger->set_clip_contents(true);
    trigger->set_toggle_mode(true);
    trigger->set_custom_minimum_size(Vector2(pixels(this, 72), pixels(this, 24)));
    trigger_marker = add<TextureRect>(trigger);
    trigger_marker->set_mouse_filter(MOUSE_FILTER_IGNORE);
    trigger_marker->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    trigger_marker->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    trigger_marker->set_size(Vector2(pixels(this, 10), pixels(this, 10)));
    trigger_marker->set_anchors_and_offsets_preset(PRESET_CENTER_LEFT, PRESET_MODE_KEEP_SIZE, pixels(this, spacing::md));
    trigger_marker->hide();
    popup_panel = add<PopupPanel>(this);
    popup_panel->hide();
    popup_panel->set_wrap_controls(true);
    if (mode == PickerMode::COMPACT) {
        popup_panel->set_name("SelectPopup");
        popup_panel->set_theme_type_variation(select_style == SelectStyle::STANDARD ? "CrimsonSelectPopup" : "CrimsonMenuPopup");
    }
    popup_panel->connect("popup_hide", callable_mp(this, &Picker::restore_focus));
    auto *content = add<VBoxContainer>(popup_panel);
    content->add_theme_constant_override("separation", pixels(this, 6));
    if (mode == PickerMode::SEARCHABLE) {
        search = popup_search_input(content, "Search controls", "Filter the available options by name or category.");
        search->set_custom_minimum_size(Vector2(0, pixels(this, 36)));
        add<HSeparator>(content);
        search->connect("text_changed", callable_mp(this, &Picker::filter));
        search->connect("text_submitted", callable_mp(this, &Picker::submit_search));
        auto *current_inset = add<MarginContainer>(content);
        current_inset->add_theme_constant_override("margin_left", pixels(this, 8));
        current_inset->add_theme_constant_override("margin_right", pixels(this, 8));
        current_row = add<HBoxContainer>(current_inset);
        current_value = label(current_row, {}, LabelStyle::MUTED);
        current_value->set_h_size_flags(SIZE_EXPAND_FILL);
        label(current_row, "Current", LabelStyle::MUTED);
        current_row->hide();
    }
    scroll = add<ScrollContainer>(content);
    if (mode == PickerMode::SEARCHABLE) {
        scroll->set_theme_type_variation("CrimsonPickerResults");
    }
    scroll->set_h_size_flags(SIZE_EXPAND_FILL);
    scroll->set_v_size_flags(SIZE_EXPAND_FILL);
    scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    scroll->set_follow_focus(true);
    list = add<VBoxContainer>(scroll);
    list->set_h_size_flags(SIZE_EXPAND_FILL);
    list->add_theme_constant_override("separation", mode == PickerMode::COMPACT ? 0 : pixels(this, 1));
    auto *empty_inset = add<MarginContainer>(content);
    if (search) {
        empty_inset->add_theme_constant_override("margin_left", pixels(this, spacing::md));
        empty_inset->set_custom_minimum_size(Vector2(0, pixels(this, 32)));
    }
    empty = label(empty_inset, "No matching options", LabelStyle::MUTED);
    empty->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    empty_inset->hide();
}

void Picker::clear_rows() {
    while (list->get_child_count() > 0) {
        Node *old = list->get_child(0);
        list->remove_child(old);
        old->queue_free();
    }
    rows.clear();
    checks.clear();
    group_buttons.clear();
    group_names.clear();
}

void Picker::set_options(std::vector<PickerOption> items) {
    if (popup_panel->is_visible()) {
        popup_panel->hide();
    }
    for (const auto &option : options) {
        if (option.content && option.content->get_parent() == this) {
            remove_child(option.content);
            option.content->queue_free();
        }
        if (option.selected_content) {
            trigger->remove_child(option.selected_content);
            option.selected_content->queue_free();
        }
    }
    clear_rows();
    for (auto &option : items) {
        if (option.content) {
            if (option.content->get_parent()) {
                option.content = nullptr;
            } else {
                // Adopt the node even when this list is never opened.
                add_child(option.content);
                option.content->hide();
            }
        }
        if (option.selected_content) {
            if (option.selected_content->get_parent()) {
                option.selected_content = nullptr;
            } else {
                add_child(option.selected_content);
                display_content(option.selected_content, trigger, 28);
                option.selected_content->hide();
            }
        }
    }
    options = std::move(items);
    collapsed_groups.clear();
    for (const auto &option : options) {
        if (!option.group.is_empty() && std::find(collapsed_groups.begin(), collapsed_groups.end(), option.group) == collapsed_groups.end()) {
            collapsed_groups.push_back(option.group);
        }
    }
    dirty = true;
    set_state(availability);
    refresh_selection();
}

void Picker::build_rows() {
    dirty = false;
    const int font_size = popup_panel->get_theme_font_size("font_size", "PopupMenu");
    const bool menu_style = select_style == SelectStyle::MENU;
    const int compact_row_height = menu_style ?
            std::ceil(popup_panel->get_theme_font("font", "PopupMenu")->get_height(font_size)) +
                    popup_panel->get_theme_constant("v_separation", "PopupMenu") :
            pixels(this, 24);
    const StringName option_style = menu_style ? "CrimsonMenuSelectOption" : "CrimsonCompactSelectOption";
    const StringName active_style = menu_style ? "CrimsonMenuSelectCandidate" : "CrimsonCompactSelectCandidate";
    for (int index = 0; index < static_cast<int>(options.size()); ++index) {
        const auto &option = options[index];
        VBoxContainer *group_list = list;
        if (!option.group.is_empty()) {
            const auto group = std::find(group_names.begin(), group_names.end(), option.group);
            if (group == group_names.end()) {
                group_names.push_back(option.group);
                group_list = add<VBoxContainer>(list);
                group_list->set_h_size_flags(SIZE_EXPAND_FILL);
                group_list->add_theme_constant_override("separation", pixels(this, 1));
                auto *heading = button(group_list, option.group, "Show or hide options in this category.",
                        callable_mp(this, &Picker::toggle_group).bind(option.group), ButtonStyle::QUIET);
                heading->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
                heading->add_theme_font_size_override("font_size", pixels(this, 11));
                heading->add_theme_color_override("font_color", color(Surface::MUTED));
                group_buttons.push_back(heading);
            } else {
                const auto offset = std::distance(group_names.begin(), group);
                group_list = Object::cast_to<VBoxContainer>(group_buttons[offset]->get_parent());
            }
        }
        auto *frame = add<Control>(group_list);
        frame->set_mouse_filter(MOUSE_FILTER_IGNORE);
        frame->set_clip_contents(true);
        frame->set_h_size_flags(SIZE_EXPAND_FILL);
        Button *row = button(frame, option.content ? String() : option.label, {},
                callable_mp(this, &Picker::choose).bind(index), ButtonStyle::ROW);
        if (!option.content && option.leading_icon.is_valid()) {
            row->set_button_icon(option.leading_icon);
            row->set_icon_alignment(HORIZONTAL_ALIGNMENT_LEFT);
            for (const char *state : {"icon_normal_color", "icon_hover_color", "icon_focus_color",
                         "icon_pressed_color", "icon_hover_pressed_color"}) {
                row->add_theme_color_override(state, Color(1, 1, 1));
            }
        }
        if (mode == PickerMode::COMPACT) {
            row->set_theme_type_variation(option_style);
            row->set_custom_minimum_size(Vector2());
            tooltip(row, option.label);
            row->connect("focus_entered", callable_mp(static_cast<Control *>(row), &Control::set_theme_type_variation).bind(active_style));
            row->connect("focus_exited", callable_mp(static_cast<Control *>(row), &Control::set_theme_type_variation).bind(option_style));
            row->connect("mouse_entered", callable_mp(static_cast<Control *>(row), &Control::grab_focus).bind(false));
        } else {
            row->set_theme_type_variation("CrimsonPickerOption");
        }
        row->set_accessibility_name(option.label);
        row->set_disabled(option.disabled);
        row->set_focus_mode(option.disabled ? FOCUS_NONE : FOCUS_ALL);
        row->set_default_cursor_shape(option.disabled ? CURSOR_FORBIDDEN : CURSOR_POINTING_HAND);
        row->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        row->set_clip_text(true);
        row->set_toggle_mode(true);
        row->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
        const real_t content_height = option.content ? option.content->get_combined_minimum_size().y : 0;
        const int row_height = mode == PickerMode::COMPACT ? compact_row_height : pixels(this, option.content ? 40 : 32);
        frame->set_custom_minimum_size(Vector2(0, std::max<real_t>(row_height, content_height)));
        if (option.content) {
            display_content(option.content, frame, 26);
            option.content->show();
        }
        auto *check = add<TextureRect>(frame);
        check->set_name("SelectedCheck");
        check->set_texture(icon(popup_panel, mode == PickerMode::COMPACT ? "check" : "selected_check"));
        check->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
        check->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
        check->set_mouse_filter(MOUSE_FILTER_IGNORE);
        // Set the end anchors first because Control clamps each start anchor to its current end anchor.
        const int end_padding = mode == PickerMode::COMPACT ? 8 : 4;
        check->set_anchor_and_offset(SIDE_RIGHT, 1.0, -pixels(this, end_padding));
        check->set_anchor_and_offset(SIDE_BOTTOM, 0.5, pixels(this, 8));
        check->set_anchor_and_offset(SIDE_LEFT, 1.0, -pixels(this, end_padding + 16));
        check->set_anchor_and_offset(SIDE_TOP, 0.5, -pixels(this, 8));
        checks.push_back(check);
        rows.push_back(row);
    }
    refresh_selection();
}

void Picker::refresh_selection() {
    trigger->set_text(placeholder);
    trigger->set_accessibility_name(placeholder);
    const bool has_rich_value = std::any_of(options.begin(), options.end(), [](const PickerOption &entry) { return entry.selected_content; });
    if (has_rich_value) {
        const auto chosen = std::find_if(options.begin(), options.end(), [&](const PickerOption &entry) { return entry.id == selection; });
        const real_t content_height = chosen != options.end() && chosen->selected_content ? chosen->selected_content->get_combined_minimum_size().y : 0;
        trigger->set_custom_minimum_size(Vector2(pixels(this, 72), std::max<real_t>(pixels(this, 40), content_height)));
    } else if (rich_trigger) {
        trigger->set_custom_minimum_size(Vector2(pixels(this, 72), pixels(this, 24)));
    }
    rich_trigger = has_rich_value;
    Ref<Texture2D> selected_marker;
    if (current_row) {
        current_row->hide();
    }
    for (int index = 0; index < static_cast<int>(options.size()); ++index) {
        const bool chosen = options[index].id == selection;
        if (options[index].selected_content) {
            options[index].selected_content->set_visible(chosen);
        }
        if (chosen) {
            trigger->set_text(options[index].selected_content ? String() : options[index].label);
            trigger->set_accessibility_name(options[index].label);
            if (!options[index].selected_content) {
                selected_marker = options[index].leading_icon;
            }
            if (current_row) {
                current_value->set_text(options[index].label);
                current_row->set_visible(search->get_text().is_empty());
            }
        }
        if (index < static_cast<int>(rows.size())) {
            rows[index]->set_pressed_no_signal(chosen);
            checks[index]->set_visible(chosen);
        }
    }
    show_trigger_marker(selected_marker);
}

void Picker::show_trigger_marker(const Ref<Texture2D> &marker) {
    const bool shown = marker.is_valid();
    const bool visibility_changed = shown != trigger_marker->is_visible();
    trigger_marker->set_texture(marker);
    trigger_marker->set_visible(shown);
    if (!visibility_changed) {
        return;
    }
    for (const char *state : {"normal", "hover", "pressed", "hover_pressed", "disabled"}) {
        if (shown) {
            const Ref<StyleBox> style = trigger->get_theme_stylebox(state);
            const Ref<StyleBox> inset = style->duplicate();
            inset->set_content_margin(SIDE_LEFT, style->get_margin(SIDE_LEFT) + pixels(this, 14));
            trigger->add_theme_stylebox_override(state, inset);
        } else {
            trigger->remove_theme_stylebox_override(state);
        }
    }
}

void Picker::filter(const String &text) {
    if (dirty) {
        return;
    }
    const String query = text.strip_edges().to_lower();
    if (current_row) {
        current_row->set_visible(query.is_empty() && !selection.is_empty());
    }
    bool any_match = false;
    for (int index = 0; index < static_cast<int>(options.size()); ++index) {
        const auto &option = options[index];
        const bool matches = query.is_empty() || option.label.to_lower().contains(query) || option.group.to_lower().contains(query);
        const bool expanded = !query.is_empty() || std::find(collapsed_groups.begin(), collapsed_groups.end(), option.group) == collapsed_groups.end();
        Object::cast_to<Control>(rows[index]->get_parent())->set_visible(matches && expanded);
        any_match |= matches;
    }
    for (int index = 0; index < static_cast<int>(group_names.size()); ++index) {
        const String group = group_names[index];
        const bool matches = query.is_empty() || std::any_of(options.begin(), options.end(), [&](const PickerOption &item) {
            return item.group == group && (item.label.to_lower().contains(query) || group.to_lower().contains(query));
        });
        const bool expanded = !query.is_empty() || std::find(collapsed_groups.begin(), collapsed_groups.end(), group) == collapsed_groups.end();
        group_buttons[index]->set_button_icon(icon(group_buttons[index], expanded ? "chevron_down" : "chevron_right"));
        Object::cast_to<Control>(group_buttons[index]->get_parent())->set_visible(matches);
    }
    Object::cast_to<Control>(empty->get_parent())->set_visible(!any_match);
    if (search) {
        scroll->set_visible(any_match);
        if (popup_panel->is_visible()) {
            fit_searchable_popup();
        }
    }
}

int Picker::searchable_height() const {
    constexpr int maximum_result_rows = 8;
    const int row_gap = list->get_theme_constant("separation");
    const int list_height = std::ceil(list->get_minimum_size().y);
    const auto first_heading = std::find_if(group_buttons.begin(), group_buttons.end(), [](const Button *heading) {
        return Object::cast_to<Control>(heading->get_parent())->is_visible();
    });
    const int heading_height = first_heading == group_buttons.end() ? 0 :
            std::ceil((*first_heading)->get_combined_minimum_size().y) + row_gap;
    const int result_limit = maximum_result_rows * pixels(scroll, 32) + (maximum_result_rows - 1) * row_gap + heading_height;
    const int chrome_height = std::ceil(popup_panel->get_contents_minimum_size().y);
    return chrome_height + (scroll->is_visible() ? std::min(list_height, result_limit) : 0);
}

void Picker::fit_searchable_popup() {
    const Vector2i previous = popup_panel->get_size();
    const int height = std::clamp(searchable_height(), 1, searchable_height_limit);
    if (height == previous.y) {
        return;
    }
    popup_panel->set_size(Vector2i(previous.x, height));
    if (searchable_above_trigger) {
        popup_panel->set_position(popup_panel->get_position() + Vector2i(0, previous.y - popup_panel->get_size().y));
    }
}

void Picker::toggle_group(String group) {
    const auto found = std::find(collapsed_groups.begin(), collapsed_groups.end(), group);
    if (found == collapsed_groups.end()) {
        collapsed_groups.push_back(group);
    } else {
        collapsed_groups.erase(found);
    }
    filter(search ? search->get_text() : String());
}

void Picker::open() {
    if (options.empty() || (availability != FieldState::EDITABLE && availability != FieldState::INVALID)) {
        trigger->set_pressed_no_signal(false);
        return;
    }
    if (dirty) {
        build_rows();
    }
    if (search) {
        search->set_text({});
    }
    filter({});
    const Vector2 origin = trigger->get_global_position();
    const bool compact = mode == PickerMode::COMPACT;
    Rect2 usable = get_viewport()->get_visible_rect();
    if ((compact || search) && !popup_panel->is_embedded()) {
        // A native popup can use the monitor around its owner, even for a short dialog.
        // popup_on_parent() expects these bounds in the visible owner's coordinates.
        if (const auto *window = get_window(); window && !window->is_embedded()) {
            const auto screen = DisplayServer::get_singleton()->screen_get_usable_rect(window->get_current_screen());
            if (screen.has_area()) {
                usable = Rect2(screen.position - window->get_position(), screen.size);
            }
        }
    }
    const int default_width = compact ? 1 : pixels(this, mode == PickerMode::SEARCHABLE ? 272 : 180);
    const int minimum_width = std::max(default_width, pixels(this, popup_width_minimum));
    int desired_width = std::max<int>(trigger->get_size().x, minimum_width);
    if (popup_width_limit > 0) {
        desired_width = std::min(desired_width, std::max(minimum_width, pixels(this, popup_width_limit)));
    }
    const int width = std::max(1, std::min<int>(usable.size.x, desired_width));
    const Ref<StyleBox> panel = popup_panel->get_theme_stylebox("panel");
    int desired_height = panel.is_valid() ? std::ceil(panel->get_minimum_size().y) : 0;
    if (search) {
        desired_height = searchable_height();
    } else {
        for (const auto *row : rows) {
            const auto *frame = Object::cast_to<Control>(row->get_parent());
            desired_height += static_cast<int>(frame->get_combined_minimum_size().y) + (compact ? 0 : pixels(this, 1));
        }
        desired_height = std::min(desired_height, pixels(this, compact ? 248 : 230));
    }
    const int gap = pixels(this, compact ? 5 : 4);
    const int below = std::max(0, static_cast<int>(usable.get_end().y - origin.y - trigger->get_size().y) - gap);
    const int above = std::max(0, static_cast<int>(origin.y - usable.position.y) - gap);
    const bool flip = desired_height > below && above > below;
    const int room = std::max(1, flip ? above : below);
    const int height = std::clamp(desired_height, 1, room);
    if (search) {
        searchable_height_limit = room;
        searchable_above_trigger = flip;
    }
    const int x = std::clamp<int>(origin.x, usable.position.x, usable.get_end().x - width);
    const int y = flip ? static_cast<int>(origin.y) - height - gap : static_cast<int>(origin.y + trigger->get_size().y) + gap;
    trigger->set_pressed_no_signal(true);
    popup_panel->popup_on_parent(Rect2i(x, y, width, height));
    if (search) {
        search->grab_focus();
        return;
    }
    const auto selected = std::find_if(options.begin(), options.end(), [&](const PickerOption &entry) { return entry.id == selection; });
    if (selected != options.end() && !selected->disabled && rows[std::distance(options.begin(), selected)]->is_visible_in_tree()) {
        rows[std::distance(options.begin(), selected)]->grab_focus();
        return;
    }
    for (auto *row : rows) {
        if (row->is_visible_in_tree() && !row->is_disabled()) {
            row->grab_focus();
            return;
        }
    }
    if (!group_buttons.empty()) {
        group_buttons.front()->grab_focus();
    }
}

void Picker::restore_focus() {
    trigger->set_pressed_no_signal(false);
    if (is_inside_tree() && trigger->is_visible_in_tree() && !trigger->is_disabled()) {
        trigger->grab_focus();
    }
}

void Picker::choose(int index) {
    if (index >= 0 && index < static_cast<int>(options.size()) && !options[index].disabled) {
        const bool changed = selection != options[index].id;
        selection = options[index].id;
        refresh_selection();
        popup_panel->hide();
        if (on_chosen.is_valid()) {
            if (mode == PickerMode::COMPACT) {
                if (changed) {
                    on_chosen.call(index);
                }
            } else {
                on_chosen.call(selection);
            }
        }
    }
}

void Picker::submit_search(const String &) {
    for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
        if (rows[index]->is_visible_in_tree() && !rows[index]->is_disabled()) {
            choose(index);
            return;
        }
    }
}

void Picker::set_selected(const StringName &id) {
    selection = id;
    refresh_selection();
}

void Picker::set_state(FieldState state) {
    availability = state;
    const bool disabled = options.empty() || state == FieldState::READ_ONLY || state == FieldState::DISABLED;
    trigger->set_disabled(disabled);
    trigger->set_focus_mode(disabled ? FOCUS_NONE : FOCUS_ALL);
    trigger->set_default_cursor_shape(state == FieldState::READ_ONLY ? CURSOR_ARROW : disabled ? CURSOR_FORBIDDEN : CURSOR_POINTING_HAND);
}

void Picker::set_popup_width_limit(int logical_width) {
    popup_width_limit = std::max(0, logical_width);
}

void Picker::set_popup_minimum_width(int logical_width) {
    popup_width_minimum = std::max(0, logical_width);
}

void Picker::set_search_placeholder(const String &text) {
    if (search) {
        search->set_placeholder(text);
        search->set_accessibility_name(text);
    }
}

Button *Picker::get_trigger() const {
    return trigger;
}

StringName Picker::get_selected() const {
    return selection;
}

void Picker::set_items(const PackedStringArray &items, int selected) {
    std::vector<PickerOption> values;
    values.reserve(items.size());
    for (int index = 0; index < items.size(); ++index) {
        values.push_back({StringName(String::num_int64(index)), items[index], {}});
    }
    set_options(std::move(values));
    select(selected);
}

void Picker::select(int selected) {
    set_selected(selected >= 0 && selected < static_cast<int>(options.size()) ? options[selected].id : StringName());
}

int Picker::get_selected_index() const {
    const auto selected = std::find_if(options.begin(), options.end(), [&](const PickerOption &option) {
        return option.id == selection;
    });
    return selected == options.end() ? -1 : static_cast<int>(std::distance(options.begin(), selected));
}

void Picker::set_placeholder(const String &prompt) {
    placeholder = prompt;
    refresh_selection();
}

void Picker::set_item_disabled(int index, bool disabled) {
    if (index < 0 || index >= static_cast<int>(options.size()) || options[index].disabled == disabled) {
        return;
    }
    options[index].disabled = disabled;
    if (!dirty) {
        auto *row = rows[index];
        row->set_disabled(disabled);
        row->set_focus_mode(disabled ? FOCUS_NONE : FOCUS_ALL);
        row->set_default_cursor_shape(disabled ? CURSOR_FORBIDDEN : CURSOR_POINTING_HAND);
    }
}

Picker *picker(Node *owner, const String &placeholder, const Callable &chosen, bool searchable) {
    auto *control = add<Picker>(owner);
    control->configure(placeholder, chosen, searchable ? PickerMode::SEARCHABLE : PickerMode::RICH);
    return control;
}

Picker *select(Node *owner, const PackedStringArray &options, int selected, const Callable &changed, FieldState state,
        SelectStyle style) {
    auto *control = add<Picker>(owner);
    control->set_name("CompactSelect");
    control->set_custom_minimum_size(Vector2(pixels(owner, 100), pixels(owner, 24)));
    control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    control->configure("", changed, PickerMode::COMPACT, style);
    control->set_items(options, selected);
    control->set_state(state);
    return control;
}

} // namespace crimson::editor_ui

#endif
