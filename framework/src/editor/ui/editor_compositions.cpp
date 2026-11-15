// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "editor_compositions.hpp"

#ifdef DEBUG_ENABLED

#include "editor_spacing.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/h_split_container.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/v_split_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>

namespace crimson::editor_ui {
using namespace godot;

namespace {

/// Create an owned stock control, or leave it detached for a content slot.
template <typename T>
T *add(Node *owner) {
    T *control = memnew(T);
    if (owner) {
        owner->add_child(control);
    }
    return control;
}

/// Add padding to a rectangular content slot.
void margins(MarginContainer *margin, int horizontal, int vertical) {
    margin->add_theme_constant_override("margin_left", pixels(margin, horizontal));
    margin->add_theme_constant_override("margin_right", pixels(margin, horizontal));
    margin->add_theme_constant_override("margin_top", pixels(margin, vertical));
    margin->add_theme_constant_override("margin_bottom", pixels(margin, vertical));
}

/// Add a content region with deliberate padding and an optional structural divider.
void card_slot(VBoxContainer *stack, Control *slot, bool divide, bool header = false) {
    if (!slot) {
        return;
    }
    if (divide && stack->get_child_count() > 0) {
        add<HSeparator>(stack);
    }
    auto *padding = add<MarginContainer>(stack);
    margins(padding, 12, header ? 8 : 10);
    padding->add_child(slot);
}

/// Make the native splitter's keyboard and controller handling reachable by focus.
void keyboard_split(SplitContainer *split) {
    const TypedArray<Control> handles = split->get_drag_area_controls();
    for (int index = 0; index < handles.size(); ++index) {
        Object *handle_object = handles[index];
        auto *handle = Object::cast_to<Control>(handle_object);
        if (!handle) {
            continue;
        }
        handle->set_focus_mode(Control::FOCUS_ALL);
        handle->set_accessibility_name("Resize panels");
        tooltip(handle, "Drag or use arrow keys to adjust the panel sizes.");
    }
}

/// Attach two detached panes and enable native pointer and focus-driven resizing.
template <typename T>
T *split(Node *owner, Control *first, Control *second) {
    auto *result = add<T>(owner);
    result->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    result->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    for (auto *pane : {first, second}) {
        if (pane) {
            result->add_child(pane);
        }
    }
    keyboard_split(result);
    return result;
}

} // namespace

void Tree::configure(const Callable &selection_changed) {
    on_selection = selection_changed;
    set_h_size_flags(SIZE_EXPAND_FILL);
    add_theme_constant_override("separation", pixels(this, 1));
}

void Tree::add_row(const StringName &id, Control *body, const StringName &parent, Control *actions, bool expanded,
        const String &help, Color background) {
    const auto containing = rows.find(parent);
    if (id.is_empty() || rows.find(id) != rows.end() || (!parent.is_empty() && containing == rows.end()) ||
            !body || body->get_parent() || (actions && actions->get_parent())) {
        return;
    }
    auto *parent_branch = containing == rows.end() ? this : containing->second.children;
    auto *branch = add<VBoxContainer>(parent_branch);
    branch->set_h_size_flags(SIZE_EXPAND_FILL);
    branch->add_theme_constant_override("separation", pixels(this, 1));
    auto *target = button(branch, {}, help, callable_mp(this, &Tree::activate).bind(id), ButtonStyle::ROW);
    if (background.a > 0) {
        target->add_theme_stylebox_override("normal", rounded_box(background, Color(0, 0, 0, 0),
                0, 0, get_theme_default_base_scale()));
    }
    target->set_toggle_mode(true);
    target->set_h_size_flags(SIZE_EXPAND_FILL);
    target->set_custom_minimum_size(Vector2(0, pixels(this, 28)));
    if (help.is_empty()) {
        if (const auto *caption = Object::cast_to<Label>(body)) {
            target->set_accessibility_name(caption->get_text());
        }
    } else {
        target->set_accessibility_name(help);
    }
    target->connect("gui_input", callable_mp(this, &Tree::navigate).bind(id));
    auto *line = add<HBoxContainer>(target);
    line->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
    line->set_offset(SIDE_LEFT, pixels(this, parent.is_empty() ? 8 : 5));
    line->set_offset(SIDE_RIGHT, -pixels(this, 8));
    line->set_mouse_filter(MOUSE_FILTER_IGNORE);
    line->add_theme_constant_override("separation", pixels(this, 5));
    auto *expand = icon_button(line, expanded ? "chevron_down" : "chevron_right", "Expand or collapse this branch.",
            callable_mp(this, &Tree::toggle).bind(id), ButtonStyle::QUIET);
    expand->set_custom_minimum_size(Vector2(pixels(this, 18), pixels(this, 24)));
    expand->set_focus_mode(FOCUS_NONE);
    // Reserve indentation on leaf rows without exposing a false disclosure action.
    expand->hide();
    auto *spacer = add<Control>(line);
    spacer->set_mouse_filter(MOUSE_FILTER_IGNORE);
    spacer->set_custom_minimum_size(Vector2(pixels(this, parent.is_empty() ? 18 : 7), 0));
    line->add_child(body);
    body->set_h_size_flags(SIZE_EXPAND_FILL);
    body->set_v_size_flags(SIZE_SHRINK_CENTER);
    body->set_mouse_filter(MOUSE_FILTER_IGNORE);
    if (actions) {
        line->add_child(actions);
        actions->set_v_size_flags(SIZE_SHRINK_CENTER);
    }
    target->set_custom_minimum_size(Vector2(0, std::max<double>(pixels(this, 28), line->get_combined_minimum_size().y)));
    line->connect("minimum_size_changed", callable_mp_static(+[](HBoxContainer *content, Button *surface) {
        surface->set_custom_minimum_size(Vector2(0, std::max<double>(pixels(surface, 28), content->get_combined_minimum_size().y)));
    }).bind(line, target));
    auto *guide = add<PanelContainer>(branch);
    guide->set_mouse_filter(MOUSE_FILTER_IGNORE);
    auto guide_box = box(Color(0, 0, 0, 0), color(Surface::RULE), 0, 0, get_theme_default_base_scale());
    guide_box->set_border_width_all(0);
    guide_box->set_border_width(SIDE_LEFT, pixels(this, 1));
    guide->add_theme_stylebox_override("panel", guide_box);
    auto *indent = add<MarginContainer>(guide);
    indent->add_theme_constant_override("margin_left", pixels(this, 14));
    auto *children = add<VBoxContainer>(indent);
    children->set_h_size_flags(SIZE_EXPAND_FILL);
    children->add_theme_constant_override("separation", 0);
    guide->hide();
    rows.emplace(id, Row{parent, branch, target, line, expand, spacer, guide, children, {}, expanded});
    if (containing == rows.end()) {
        roots.push_back(id);
    } else {
        auto &parent_row = containing->second;
        parent_row.order.push_back(id);
        parent_row.expand->show();
        parent_row.spacer->hide();
        parent_row.guide->set_visible(parent_row.expanded);
    }
}

void Tree::clear() {
    rows.clear();
    roots.clear();
    selected = {};
    while (get_child_count() > 0) {
        Node *old = get_child(0);
        remove_child(old);
        old->queue_free();
    }
}

void Tree::activate(StringName id) {
    set_selected(id);
    if (on_selection.is_valid()) {
        on_selection.call(id);
    }
}

void Tree::set_selected(const StringName &id, bool focus) {
    if (const auto previous = rows.find(selected); previous != rows.end()) {
        previous->second.target->set_pressed_no_signal(false);
    }
    if (const auto current = rows.find(id); current != rows.end()) {
        selected = id;
        current->second.target->set_pressed_no_signal(true);
        if (focus && current->second.target->is_visible_in_tree()) {
            current->second.target->grab_focus();
        }
    } else {
        selected = {};
    }
}

StringName Tree::get_selected() const {
    return selected;
}

void Tree::set_expanded(const StringName &id, bool expanded) {
    if (const auto found = rows.find(id); found != rows.end()) {
        auto &row = found->second;
        row.expanded = expanded;
        row.guide->set_visible(expanded && !row.order.empty());
        row.expand->set_button_icon(icon(row.expand, expanded ? "chevron_down" : "chevron_right"));
    }
}

bool Tree::is_expanded(const StringName &id) const {
    const auto row = rows.find(id);
    return row != rows.end() && row->second.expanded;
}

void Tree::toggle(StringName id) {
    set_expanded(id, !is_expanded(id));
}

void Tree::visible_rows(const std::vector<StringName> &ids, std::vector<StringName> &visible) const {
    for (const auto &id : ids) {
        const auto &row = rows.at(id);
        visible.push_back(id);
        if (row.expanded) {
            visible_rows(row.order, visible);
        }
    }
}

void Tree::navigate(const Ref<InputEvent> &event, StringName id) {
    const auto current = rows.find(id);
    if (current == rows.end()) {
        return;
    }
    const auto &row = current->second;
    StringName next;
    if (event->is_action_pressed("ui_right", true)) {
        if (!row.order.empty()) {
            if (row.expanded) {
                next = row.order.front();
            } else {
                set_expanded(id, true);
            }
        }
    } else if (event->is_action_pressed("ui_left", true)) {
        if (!row.order.empty() && row.expanded) {
            set_expanded(id, false);
        } else {
            next = row.parent;
        }
    } else {
        const bool down = event->is_action_pressed("ui_down", true);
        const bool up = event->is_action_pressed("ui_up", true);
        if (!down && !up) {
            return;
        }
        std::vector<StringName> visible;
        visible_rows(roots, visible);
        const auto found = std::find(visible.begin(), visible.end(), id);
        if (found != visible.end() && ((down && std::next(found) != visible.end()) || (up && found != visible.begin()))) {
            next = *(down ? std::next(found) : std::prev(found));
        }
    }
    row.target->accept_event();
    if (!next.is_empty()) {
        rows.at(next).target->grab_focus();
    }
}

Tree *tree(Node *owner, const Callable &selection_changed) {
    auto *result = add<Tree>(owner);
    result->configure(selection_changed);
    return result;
}

PanelContainer *surface_panel(Node *owner, Surface role) {
    auto *panel = add<PanelContainer>(owner);
    panel->add_theme_stylebox_override("panel", box(color(role), Color(0, 0, 0, 0)));
    return panel;
}

MarginContainer *inset(Node *owner, int horizontal, int vertical) {
    auto *content = add<MarginContainer>(owner);
    content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    margins(content, horizontal, vertical);
    return content;
}

PanelContainer *card(Node *owner, Control *header, Control *body, Control *footer, Color background) {
    auto *panel = add<PanelContainer>(owner);
    panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    auto frame = rounded_box(background, color(Surface::RULE), 1, 1, panel->get_theme_default_base_scale());
    panel->add_theme_stylebox_override("panel", frame);
    auto *stack = add<VBoxContainer>(panel);
    stack->add_theme_constant_override("separation", 0);
    card_slot(stack, header, false, true);
    card_slot(stack, body, false);
    card_slot(stack, footer, true);
    return panel;
}

HBoxContainer *field(Node *owner, const String &name, Control *value, const String &help, int label_width) {
    auto *row = add<HBoxContainer>(owner);
    row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    row->set_custom_minimum_size(Vector2(0, pixels(row, 28)));
    row->add_theme_constant_override("separation", pixels(row, spacing::md));
    auto *caption = label(row, name, LabelStyle::MUTED);
    caption->set_custom_minimum_size(Vector2(pixels(row, label_width), 0));
    caption->set_h_size_flags(value ? Control::SIZE_EXPAND_FILL : Control::SIZE_FILL);
    caption->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    caption->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    tooltip(caption, help.is_empty() ? name : help);
    if (value && !value->get_parent()) {
        row->add_child(value);
        value->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        value->set_stretch_ratio(1.45f);
        value->set_accessibility_name(name);
        tooltip(value, help);
        if (auto *select = Object::cast_to<Picker>(value)) {
            tooltip(select->get_trigger(), help);
        }
    }
    return row;
}

VBoxContainer *field_group(Node *owner, const String &heading) {
    auto *group = add<VBoxContainer>(owner);
    group->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    group->add_theme_constant_override("separation", pixels(group, 5));
    if (!heading.is_empty()) {
        label(group, heading, LabelStyle::HEADING);
    }
    return group;
}

PanelContainer *empty_state(Node *owner, const String &title, const String &description, Control *action, Control *media) {
    auto *panel = add<PanelContainer>(owner);
    const Color no_border(0, 0, 0, 0);
    panel->add_theme_stylebox_override("panel", rounded_box(color(Surface::SIDEBAR), no_border,
            0, 0, panel->get_theme_default_base_scale()));
    panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    auto *inset = add<MarginContainer>(panel);
    margins(inset, 12, 14);
    auto *content = add<VBoxContainer>(inset);
    content->add_theme_constant_override("separation", pixels(content, 6));
    if (media) {
        content->add_child(media);
    }
    label(content, title, LabelStyle::HEADING);
    auto *message = label(content, description, LabelStyle::MUTED);
    message->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    if (action) {
        action->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
        content->add_child(action);
    }
    return panel;
}

HBoxContainer *item_row(Node *owner, const String &title, const String &description, Control *media, Control *action) {
    auto *row = add<HBoxContainer>(owner);
    row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    row->set_custom_minimum_size(Vector2(0, pixels(row, description.is_empty() ? 24 : 32)));
    row->add_theme_constant_override("separation", pixels(row, 8));
    if (media) {
        row->add_child(media);
        media->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    }
    auto *text = add<VBoxContainer>(row);
    text->add_theme_constant_override("separation", 0);
    text->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    label(text, title, LabelStyle::HEADING)->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    if (!description.is_empty()) {
        auto *subtitle = label(text, description, LabelStyle::MUTED);
        subtitle->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        // A rich picker may assign its full width only after the content is attached.
        // Keep this row two lines at any width; provide its full description on hover.
        row->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        tooltip(row, description);
    }
    if (action) {
        row->add_child(action);
        action->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    }
    return row;
}

HSplitContainer *split_pane(Node *owner, Control *left, Control *right) {
    return split<HSplitContainer>(owner, left, right);
}

VSplitContainer *split_pane_vertical(Node *owner, Control *top, Control *bottom) {
    return split<VSplitContainer>(owner, top, bottom);
}

void register_editor_compositions() {
    GDREGISTER_INTERNAL_CLASS(EditorTree);
}

} // namespace crimson::editor_ui

#endif
