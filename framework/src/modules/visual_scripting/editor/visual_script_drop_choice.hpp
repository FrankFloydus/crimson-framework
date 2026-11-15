// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/style_box.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <vector>

namespace crimson::visual_scripting {

/// Show the actions for a dropped asset in the graph's own viewport, which owns their pointer and focus events.
/// The owner dismisses and frees the returned overlay. At least one option is required.
/// The activation callback receives one graph node record.
inline godot::Control *show_drop_choice(godot::Control *owner, godot::Vector2 origin, const godot::Array &options,
        const godot::Callable &activate, const godot::Callable &dismiss) {
    using namespace godot;
    namespace ui = crimson::editor_ui;

    if (options.is_empty()) {
        return nullptr;
    }

    auto *overlay = memnew(Control);
    overlay->set_name("GraphDropChoiceOverlay");
    overlay->set_mouse_filter(Control::MOUSE_FILTER_STOP);
    overlay->set_force_pass_scroll_events(false);
    overlay->set_clip_contents(true);
    owner->add_child(overlay);
    overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

    overlay->connect("gui_input", callable_mp_static(+[](const Ref<InputEvent> &event, Control *target, const Callable &cancel) {
        const Ref<InputEventMouseButton> mouse = event;
        const bool pointer_pressed = mouse.is_valid() && mouse->is_pressed() &&
                (mouse->get_button_index() == MOUSE_BUTTON_LEFT || mouse->get_button_index() == MOUSE_BUTTON_RIGHT ||
                 mouse->get_button_index() == MOUSE_BUTTON_MIDDLE);
        if (pointer_pressed) {
            target->accept_event();
            cancel.call();
        }
    }).bind(overlay, dismiss));

    auto *panel = memnew(PanelContainer);
    panel->set_mouse_filter(Control::MOUSE_FILTER_STOP);
    panel->set_force_pass_scroll_events(false);
    overlay->add_child(panel);
    panel->add_theme_stylebox_override("panel", panel->get_theme_stylebox("panel", "CrimsonOverlayMenu"));

    auto *rows = memnew(VBoxContainer);
    panel->add_child(rows);
    rows->add_theme_constant_override("separation", ui::pixels(rows, ui::spacing::xxs));
    auto *heading = ui::label(rows, "Add Reference", ui::LabelStyle::MUTED);
    heading->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);

    std::vector<Button *> choices;
    for (const Variant &value : options) {
        const Dictionary choice = value;
        const Dictionary record = choice.get("node", Dictionary());
        auto *row = ui::button(rows, choice.get("label", String()), choice.get("help", String()), activate.bind(record),
                ui::ButtonStyle::ROW);
        row->set_meta("drop_kind", StringName(record.get("kind", StringName())));
        row->set_theme_type_variation("CrimsonMenuSelectOption");
        row->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        row->set_clip_text(true);
        row->set_action_mode(BaseButton::ACTION_MODE_BUTTON_RELEASE);
        row->set_focus_mode(Control::FOCUS_ALL);
        row->connect("gui_input", callable_mp_static(+[](const Ref<InputEvent> &event, Control *target, const Callable &cancel) {
            const Ref<InputEventKey> key = event;
            if (event->is_action_pressed("ui_cancel") ||
                    (key.is_valid() && key->is_pressed() && !key->is_echo() && key->get_keycode() == KEY_ESCAPE)) {
                target->accept_event();
                cancel.call();
            }
        }).bind(row, dismiss));
        choices.push_back(row);
    }

    for (size_t index = 0; index < choices.size(); ++index) {
        auto *row = choices[index];
        const NodePath previous = row->get_path_to(choices[(index + choices.size() - 1) % choices.size()]);
        const NodePath next = row->get_path_to(choices[(index + 1) % choices.size()]);
        row->set_focus_neighbor(SIDE_TOP, previous);
        row->set_focus_neighbor(SIDE_BOTTOM, next);
        row->set_focus_neighbor(SIDE_LEFT, ".");
        row->set_focus_neighbor(SIDE_RIGHT, ".");
        row->set_focus_previous(previous);
        row->set_focus_next(next);
    }

    const Callable fit_panel = callable_mp_static(+[](Control *space, PanelContainer *content, Vector2 drop_origin) {
        const Vector2 bounds = space->get_size();
        const real_t edge = ui::pixels(space, ui::spacing::xs);
        const real_t desired_width = ui::pixels(space, 224);
        const Vector2 minimum = content->get_combined_minimum_size();
        content->set_size(Vector2(std::min(desired_width, std::max(minimum.x, bounds.x - edge * 2)), minimum.y));
        const Vector2 margin(edge, edge);
        content->set_position(drop_origin.clamp(margin, (bounds - content->get_size() - margin).max(margin)));
    }).bind(overlay, panel, origin);
    overlay->connect("resized", fit_panel);
    fit_panel.call();
    choices.front()->grab_focus();
    return overlay;
}

} // namespace crimson::visual_scripting

#endif
