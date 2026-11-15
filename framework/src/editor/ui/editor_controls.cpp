// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "editor_controls.hpp"

#ifdef DEBUG_ENABLED

#include "editor_spacing.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/button_group.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/check_button.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/file_dialog.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/menu_button.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/tab_container.hpp>
#include <godot_cpp/classes/text_edit.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::editor_ui {
using namespace godot;

namespace {

/// Attach one stock control to its lifetime owner.
template <typename T>
T *add(Node *owner) {
    T *control = memnew(T);
    if (owner) {
        owner->add_child(control);
    }
    return control;
}

/// Stock button with a tooltip that presents a tool-owned shortcut as keycaps.
class ShortcutTooltipButton final : public Button {
    GDCLASS(ShortcutTooltipButton, Button)

    /// Logical keys from the tool's existing shortcut. The tooltip never handles input.
    PackedStringArray keys;

protected:
    /// This editor-only button exposes no script bindings.
    static void _bind_methods() {}

public:
    /// Resolve modifier names and the main key separately so a literal Plus key stays visible.
    void describe_shortcut(Key shortcut, const String &help) {
        auto *os = OS::get_singleton();
        keys = os->get_keycode_string(static_cast<Key>(shortcut & ~KEY_CODE_MASK)).split("+", false);
        const auto main_key = static_cast<Key>(shortcut & KEY_CODE_MASK);
        if (main_key != KEY_NONE) {
            keys.push_back(os->get_keycode_string(main_key));
        }
        const String chord = os->get_keycode_string(shortcut);
        set_accessibility_description(help.is_empty() ? chord : help + String(" ") + chord);

        // Shortcut keycaps extend 3 logical pixels into the regular tooltip's right inset.
        const Ref<StyleBox> panel = get_theme_stylebox("panel", "TooltipPanel")->duplicate();
        panel->set_content_margin(SIDE_RIGHT, std::max(0.0,
                panel->get_margin(SIDE_RIGHT) - 3.0 * get_theme_default_base_scale()));
        Ref<Theme> tooltip_theme;
        tooltip_theme.instantiate();
        tooltip_theme->set_stylebox("panel", "TooltipPanel", panel);
        set_theme(tooltip_theme);
    }

    /// Supply new tooltip contents each time Godot shows the native popup.
    Object *_make_custom_tooltip(const String &help) const override {
        auto *content = add<HBoxContainer>(nullptr);
        content->set_name("ShortcutTooltip");
        content->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        const double scale = get_theme_default_base_scale();
        content->add_theme_constant_override("separation", std::lround(spacing::md * scale));
        if (!help.is_empty()) {
            auto *description = add<Label>(content);
            description->set_theme_type_variation("TooltipLabel");
            description->set_text(help);
            description->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
            description->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
            description->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        }
        auto *keycaps = add<HBoxContainer>(content);
        keycaps->set_name("TooltipShortcutKeys");
        keycaps->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        keycaps->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
        keycaps->add_theme_constant_override("separation", std::lround(3 * scale));
        for (int64_t i = 0; i < keys.size(); ++i) {
            const String key = keys[i];
            auto *cap = add<Label>(keycaps);
            cap->set_theme_type_variation("CrimsonShortcutKey");
            cap->set_text(key == "Shift" ? String(U"\u21e7") : key);
            cap->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
            cap->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
            cap->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
            cap->set_custom_minimum_size(Vector2(std::lround(18 * scale), std::lround(19 * scale)));
            cap->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        }
        return content;
    }
};

/// Connect an optional user callback to a stock signal.
void connect(Node *node, const StringName &signal, const Callable &handler) {
    if (handler.is_valid()) {
        node->connect(signal, handler);
    }
}

/// Prevent disabled stock buttons from receiving focus and indicate that with their cursor.
void disable(BaseButton *control, bool disabled) {
    control->set_disabled(disabled);
    control->set_default_cursor_shape(disabled ? Control::CURSOR_FORBIDDEN : Control::CURSOR_POINTING_HAND);
    control->set_focus_mode(disabled ? Control::FOCUS_NONE : Control::FOCUS_ALL);
}

/// Keep read-only text copyable from the keyboard, but prevent pointer entry.
void field_state(Control *control, FieldState state, const String &help) {
    control->set_accessibility_name(help);
    tooltip(control, help);
    if (state == FieldState::READ_ONLY || state == FieldState::DISABLED) {
        const bool disabled = state == FieldState::DISABLED;
        const Color ink = disabled ? color(Surface::DISABLED) : color(Surface::TEXT);
        control->set_default_cursor_shape(disabled ? Control::CURSOR_FORBIDDEN : Control::CURSOR_ARROW);
        control->add_theme_color_override("font_uneditable_color", ink);
        control->add_theme_color_override("font_readonly_color", ink);
        if (disabled) {
            control->set_focus_mode(Control::FOCUS_NONE);
        }
        auto *shield = add<Control>(control);
        shield->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
        shield->set_mouse_filter(Control::MOUSE_FILTER_STOP);
        shield->set_focus_mode(Control::FOCUS_NONE);
        shield->set_default_cursor_shape(disabled ? Control::CURSOR_FORBIDDEN : Control::CURSOR_ARROW);
    } else if (state == FieldState::INVALID) {
        const double scale = control->get_theme_default_base_scale();
        control->add_theme_stylebox_override("normal", rounded_box(color(Surface::HEADER), color(Surface::DANGER), 9, 4, scale));
    }
}

} // namespace

Button *button(Node *owner, const String &text, const String &help, const Callable &command, ButtonStyle style, Key shortcut) {
    Button *control = nullptr;
    if (shortcut == KEY_NONE) {
        control = add<Button>(owner);
        tooltip(control, help);
    } else {
        auto *with_shortcut = add<ShortcutTooltipButton>(owner);
        // Keep a tool-owned tooltip description consistent with the editor's authored labels.
        with_shortcut->set_tooltip_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
        tooltip(with_shortcut, help);
        with_shortcut->describe_shortcut(shortcut, help);
        control = with_shortcut;
    }
    control->set_text(text);
    control->set_custom_minimum_size(Vector2(pixels(owner, 24), pixels(owner, 28)));
    control->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
    switch (style) {
        case ButtonStyle::PRIMARY: control->set_theme_type_variation("CrimsonPrimary"); break;
        case ButtonStyle::QUIET: control->set_theme_type_variation("CrimsonQuiet"); break;
        case ButtonStyle::DESTRUCTIVE: control->set_theme_type_variation("CrimsonDanger"); break;
        case ButtonStyle::ROW: control->set_theme_type_variation("CrimsonRow"); break;
        case ButtonStyle::SECONDARY: break;
    }
    connect(control, "pressed", command);
    return control;
}

Button *icon_button(Node *owner, const StringName &symbol, const String &help, const Callable &command, ButtonStyle style,
        Key shortcut) {
    auto *control = button(owner, {}, help, command, style, shortcut);
    if (style == ButtonStyle::SECONDARY) {
        control->set_theme_type_variation("CrimsonIcon");
    } else if (style == ButtonStyle::QUIET) {
        control->set_theme_type_variation("CrimsonQuietIcon");
    } else if (style == ButtonStyle::DESTRUCTIVE) {
        control->set_theme_type_variation("CrimsonDangerIcon");
    }
    if (control->is_inside_tree()) {
        control->set_button_icon(icon(control, symbol));
    } else {
        control->connect("ready", callable_mp_static(+[](Button *target, StringName name) {
            target->set_button_icon(icon(target, name));
        }).bind(control, symbol), Object::CONNECT_ONE_SHOT);
    }
    control->set_accessibility_name(help);
    control->set_custom_minimum_size(Vector2(pixels(owner, 28), pixels(owner, 28)));
    return control;
}

Label *label(Node *owner, const String &text, LabelStyle style) {
    auto *control = add<Label>(owner);
    control->set_text(text);
    control->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    switch (style) {
        case LabelStyle::MUTED: control->set_theme_type_variation("CrimsonMuted"); break;
        case LabelStyle::HEADING: control->set_theme_type_variation("CrimsonHeading"); break;
        case LabelStyle::TITLE: control->set_theme_type_variation("CrimsonTitle"); break;
        case LabelStyle::BODY: break;
    }
    return control;
}

LineEdit *text_input(Node *owner, const String &text, const String &help, FieldState state, const String &placeholder) {
    auto *control = add<LineEdit>(owner);
    control->set_custom_minimum_size(Vector2(0, pixels(owner, 28)));
    control->set_text(text);
    control->set_placeholder(placeholder);
    control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    control->set_editable(state != FieldState::DISABLED && state != FieldState::READ_ONLY);
    field_state(control, state, help);
    return control;
}

Button *drag_grip(Node *owner, const String &help) {
    auto *grip = icon_button(owner, "grip", help, {}, ButtonStyle::QUIET);
    grip->set_custom_minimum_size(Vector2(pixels(grip, 20), pixels(grip, 24)));
    grip->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    grip->set_default_cursor_shape(Control::CURSOR_DRAG);
    return grip;
}

void show_drag_preview(Control *source, const String &caption, const Ref<Texture2D> &glyph,
        const Color &glyph_color, const StringName &preview_name) {
    const auto *window = source->get_window();
    const double scale = source->get_theme_default_base_scale();
    // Godot moves the preview root to the cursor. Pad the badge itself away from the pointer.
    auto *preview = memnew(MarginContainer);
    if (!preview_name.is_empty()) {
        preview->set_name(preview_name);
    }
    if (window) {
        preview->set_theme(window->get_theme());
    }
    preview->set_mouse_behavior_recursive(Control::MOUSE_BEHAVIOR_DISABLED);
    preview->set_focus_behavior_recursive(Control::FOCUS_BEHAVIOR_DISABLED);
    preview->add_theme_constant_override("margin_left", pixels(source, spacing::lg));
    preview->add_theme_constant_override("margin_top", pixels(source, spacing::md));
    auto *badge = add<PanelContainer>(preview);
    badge->add_theme_stylebox_override("panel", rounded_box(color(Surface::RAISED), color(Surface::OUTLINE), spacing::md, spacing::xs, scale));
    auto *content = add<HBoxContainer>(badge);
    content->add_theme_constant_override("separation", pixels(source, spacing::sm));
    auto *indicator = add<TextureRect>(content);
    indicator->set_texture(glyph.is_valid() ? glyph : icon(source, "grip"));
    indicator->set_modulate(glyph_color);
    indicator->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    indicator->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    const int icon_size = pixels(source, 12);
    indicator->set_custom_minimum_size(Vector2(icon_size, icon_size));
    auto *caption_label = label(content, caption.length() > 42 ? caption.left(39) + "..." : caption, LabelStyle::BODY);
    if (!preview_name.is_empty()) {
        caption_label->set_name(String(preview_name) + "Name");
    }
    caption_label->add_theme_font_size_override("font_size", pixels(source, 12));
    source->set_drag_preview(preview);
}

Control *drag_insertion_rail(Control *owner) {
    auto *rail = memnew(PanelContainer);
    // A top-level Control can draw over a Container without participating in its layout.
    rail->set_as_top_level(true);
    rail->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    rail->set_focus_mode(Control::FOCUS_NONE);
    rail->add_theme_stylebox_override("panel", rounded_box(color(Surface::HIGHLIGHT), Color(0, 0, 0, 0),
            0, 0, owner->get_theme_default_base_scale(), 2));
    rail->hide();
    owner->add_child(rail);
    return rail;
}

void place_drag_insertion_rail(Control *rail, Rect2 row, Rect2 clip, bool after) {
    const real_t thickness = pixels(rail, 2);
    const real_t inset = pixels(rail, spacing::xs);
    const real_t y = after ? row.get_end().y : row.position.y;
    if (y < clip.position.y || y > clip.get_end().y || row.size.x <= 2 * inset || clip.size.y < thickness) {
        rail->hide();
        return;
    }
    row.position.x += inset;
    row.size.x -= 2 * inset;
    row.position.y = std::clamp(y - thickness / 2, clip.position.y, clip.get_end().y - thickness);
    row.size.y = thickness;
    const Rect2 visible = row.intersection(clip);
    rail->set_global_position(visible.position.round());
    rail->set_size(visible.size.round());
    rail->show();
    rail->move_to_front();
}

LineEdit *search_input(Node *owner, const String &placeholder, const String &help) {
    auto *control = text_input(owner, {}, help, FieldState::EDITABLE, placeholder);
    control->set_clear_button_enabled(true);
    if (control->is_inside_tree()) {
        control->set_right_icon(icon(control, "search"));
    } else {
        control->connect("ready", callable_mp_static(+[](LineEdit *entry) {
            entry->set_right_icon(icon(entry, "search"));
        }).bind(control), Object::CONNECT_ONE_SHOT);
    }
    return control;
}

LineEdit *popup_search_input(Node *owner, const String &placeholder, const String &help) {
    auto *control = search_input(owner, placeholder, help);
    control->set_theme_type_variation("CrimsonPopupSearch");
    control->set_custom_minimum_size(Vector2(0, pixels(owner, 36)));
    return control;
}

TextEdit *text_area(Node *owner, const String &text, const String &help, FieldState state) {
    auto *control = add<TextEdit>(owner);
    control->set_custom_minimum_size(Vector2(pixels(owner, 160), pixels(owner, 52)));
    control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    control->set_text(text);
    control->set_editable(state != FieldState::DISABLED && state != FieldState::READ_ONLY);
    field_state(control, state, help);
    return control;
}

void NumberInput::configure(const String &name, double initial, double minimum, double maximum, double step,
        const Callable &changed, FieldState state, const String &help) {
    lower = std::min(minimum, maximum);
    upper = std::max(minimum, maximum);
    increment = step > 0.0 ? step : 1.0;
    availability = state;
    on_change = changed;
    set_h_size_flags(SIZE_EXPAND_FILL);
    add_theme_constant_override("separation", 0);
    decrease = icon_button(this, "minus", "Decrease " + name, callable_mp(this, &NumberInput::step_by).bind(-1.0));
    entry = text_input(this, {}, help, state);
    entry->set_accessibility_name(name);
    entry->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    increase = icon_button(this, "add", "Increase " + name, callable_mp(this, &NumberInput::step_by).bind(1.0));
    for (auto *edge : {decrease, increase}) {
        edge->set_custom_minimum_size(Vector2(pixels(this, 26), pixels(this, 28)));
    }
    entry->set_custom_minimum_size(Vector2(0, pixels(this, 28)));
    const double scale = get_theme_default_base_scale();
    const Color fill = color(state == FieldState::READ_ONLY ? Surface::CANVAS : Surface::HEADER);
    const Color stroke = color(state == FieldState::INVALID ? Surface::DANGER :
            state == FieldState::READ_ONLY || state == FieldState::DISABLED ? Surface::RULE : Surface::OUTLINE);
    auto middle = box(fill, stroke, 8, 4, scale);
    middle->set_border_width(SIDE_LEFT, 0);
    middle->set_border_width(SIDE_RIGHT, 0);
    entry->add_theme_stylebox_override("normal", middle);
    entry->add_theme_stylebox_override("read_only", middle);
    const auto caps = [this, scale, stroke](bool left, Color background) {
        auto style = box(background, stroke, 0, 0, scale);
        style->set_corner_radius(left ? CORNER_TOP_LEFT : CORNER_TOP_RIGHT, pixels(this, control_corner_radius));
        style->set_corner_radius(left ? CORNER_BOTTOM_LEFT : CORNER_BOTTOM_RIGHT, pixels(this, control_corner_radius));
        style->set_anti_aliased(true);
        return style;
    };
    for (auto *edge : {decrease, increase}) {
        const auto normal = caps(edge == decrease, fill);
        edge->add_theme_stylebox_override("normal", normal);
        edge->add_theme_stylebox_override("disabled", normal);
        edge->add_theme_stylebox_override("hover", caps(edge == decrease, color(Surface::HOVER)));
        edge->add_theme_stylebox_override("pressed", caps(edge == decrease, color(Surface::SELECTED)));
    }
    entry->connect("text_submitted", callable_mp(this, &NumberInput::submit));
    entry->connect("focus_exited", callable_mp(this, &NumberInput::finish_editing));
    set_value(initial);
}

void NumberInput::apply(double next, bool notify) {
    const double last = value;
    value = std::isfinite(next) ? std::clamp(next, lower, upper) : last;
    refresh();
    if (notify && std::abs(value - last) > 1e-10 && on_change.is_valid()) {
        on_change.call(value);
    }
}

void NumberInput::refresh() {
    entry->set_text(String::num(value, 8));
    const bool disabled = availability == FieldState::DISABLED || availability == FieldState::READ_ONLY;
    disable(decrease, disabled || value <= lower);
    disable(increase, disabled || value >= upper);
}

void NumberInput::submit(const String &text) {
    if (availability == FieldState::READ_ONLY || availability == FieldState::DISABLED) {
        return;
    }
    const String normalized = text.strip_edges().replace(",", ".");
    apply(normalized.is_valid_float() ? normalized.to_float() : value, true);
}

void NumberInput::finish_editing() {
    submit(entry->get_text());
}

void NumberInput::step_by(double direction) {
    apply(value + direction * increment, true);
}

void NumberInput::set_value(double next) {
    apply(next, false);
}

void NumberInput::set_unit(const String &unit) {
    if (!unit_label) {
        unit_label = label(this, unit, LabelStyle::MUTED);
        unit_label->set_custom_minimum_size(Vector2(pixels(this, 23), 0));
        unit_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
        move_child(unit_label, increase->get_index());
    }
    unit_label->set_text(unit);
    unit_label->set_visible(!unit.is_empty());
}

void NumberInput::set_invalid_draft(const String &text) {
    if (availability == FieldState::INVALID) {
        entry->set_text(text);
    }
}

double NumberInput::get_value() const {
    return value;
}

NumberInput *number_input(Node *owner, const String &name, double value, double min, double max, double step,
        const Callable &changed, FieldState state, const String &help) {
    auto *control = add<NumberInput>(owner);
    control->configure(name, value, min, max, step, changed, state, help);
    return control;
}

HSlider *slider(Node *owner, const String &name, double value, double min, double max, double step,
        const Callable &changed, FieldState state, const String &unit) {
    auto *row = add<HBoxContainer>(owner);
    row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    auto *control = add<HSlider>(row);
    control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    control->set_custom_minimum_size(Vector2(pixels(owner, 100), pixels(owner, 28)));
    control->set_accessibility_name(name);
    control->set_min(min);
    control->set_max(max);
    control->set_step(step);
    control->set_value(value);
    control->set_scrollable(false);
    control->set_editable(state == FieldState::EDITABLE);
    control->set_default_cursor_shape(state == FieldState::DISABLED ? Control::CURSOR_FORBIDDEN :
            state == FieldState::READ_ONLY ? Control::CURSOR_ARROW : Control::CURSOR_POINTING_HAND);
    if (state != FieldState::EDITABLE) {
        control->set_focus_mode(Control::FOCUS_NONE);
    }
    auto *value_label = label(row, String::num(control->get_value(), 3) + unit);
    value_label->set_custom_minimum_size(Vector2(pixels(owner, 38), 0));
    value_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    control->connect("value_changed", callable_mp_static(+[](double current, Label *readout, String suffix) {
        readout->set_text(String::num(current, 3) + suffix);
    }).bind(value_label, unit));
    connect(control, "value_changed", changed);
    return control;
}

CheckBox *checkbox(Node *owner, const String &text, CheckState state, const Callable &toggled, bool disabled) {
    auto *control = add<CheckBox>(owner);
    control->set_text(text);
    control->set_custom_minimum_size(Vector2(0, pixels(owner, 28)));
    disable(control, disabled);
    control->set_pressed_no_signal(state == CheckState::CHECKED);
    if (state == CheckState::MIXED) {
        control->set_theme_type_variation("CrimsonMixed");
        control->set_accessibility_description("Mixed values");
        control->connect("toggled", callable_mp_static(+[](bool, CheckBox *target) {
            target->set_theme_type_variation(target->is_hovered() ? "CrimsonCheckboxHover" : "");
            target->set_accessibility_description({});
        }).bind(control), Object::CONNECT_ONE_SHOT);
    }
    if (!disabled) {
        const auto hover = callable_mp_static(+[](CheckBox *target, bool entered) {
            if (target->is_disabled() || target->get_button_group().is_valid()) {
                return;
            }
            const StringName style = target->get_theme_type_variation();
            const bool mixed = style == StringName("CrimsonMixed") || style == StringName("CrimsonMixedHover");
            if (mixed) {
                target->set_theme_type_variation(entered ? "CrimsonMixedHover" : "CrimsonMixed");
            } else {
                target->set_theme_type_variation(entered ? "CrimsonCheckboxHover" : "");
            }
        });
        control->connect("mouse_entered", hover.bind(control, true));
        control->connect("mouse_exited", hover.bind(control, false));
    }
    connect(control, "toggled", toggled);
    return control;
}

CheckButton *toggle_switch(Node *owner, const String &text, bool on, const Callable &toggled, bool disabled) {
    auto *control = add<CheckButton>(owner);
    control->set_text(text);
    control->set_custom_minimum_size(Vector2(0, pixels(owner, 28)));
    control->set_pressed_no_signal(on);
    disable(control, disabled);
    connect(control, "toggled", toggled);
    return control;
}

CheckBox *radio(Node *owner, const String &text, const Ref<ButtonGroup> &group, bool selected, const Callable &toggled, bool disabled) {
    auto *control = checkbox(owner, text, CheckState::UNCHECKED, toggled, disabled);
    control->set_button_group(group);
    control->set_pressed_no_signal(selected);
    return control;
}

Button *disclosure(Node *owner, const String &name, Control *content, bool expanded, const String &help) {
    auto *section = add<VBoxContainer>(owner);
    auto *toggle = button(section, name, help, {}, ButtonStyle::QUIET);
    toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    toggle->set_toggle_mode(true);
    toggle->set_pressed_no_signal(expanded);
    toggle->set_button_icon(icon(toggle, expanded ? "chevron_down" : "chevron_right"));
    section->add_child(content);
    content->set_visible(expanded);
    toggle->connect("toggled", callable_mp_static(+[](bool visible, Button *trigger, Control *child) {
        child->set_visible(visible);
        trigger->set_button_icon(icon(trigger, visible ? "chevron_down" : "chevron_right"));
    }).bind(toggle, content));
    return toggle;
}

TabContainer *tabs(Node *owner) {
    auto *control = add<TabContainer>(owner);
    control->set_tab_focus_mode(Control::FOCUS_ALL);
    control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    return control;
}

MenuButton *menu(Node *owner, const String &name, const String &help) {
    auto *control = add<MenuButton>(owner);
    control->set_text(name);
    control->set_button_icon(icon(control, "chevron_down"));
    control->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    control->set_custom_minimum_size(Vector2(0, pixels(owner, 28)));
    control->set_focus_mode(Control::FOCUS_ALL);
    tooltip(control, help);
    control->get_popup()->connect("popup_hide", callable_mp_static(+[](Button *trigger) {
        if (trigger->is_visible_in_tree() && !trigger->is_disabled()) {
            trigger->grab_focus();
        }
    }).bind(control));
    return control;
}

void EditorModal::configure(Node *owner, Control *background, const Callable &confirmed, const Callable &canceled) {
    workbench = background;
    on_confirmed = confirmed;
    on_canceled = canceled;
    set_name("EditorModal");
    set_anchors_and_offsets_preset(PRESET_FULL_RECT);
    set_mouse_filter(MOUSE_FILTER_STOP);
    set_z_index(100);

    scrim = add<PanelContainer>(this);
    scrim->set_name("DialogScrim");
    scrim->set_theme_type_variation("CrimsonDialogScrim");
    scrim->set_mouse_filter(MOUSE_FILTER_IGNORE);
    scrim->set_anchors_and_offsets_preset(PRESET_FULL_RECT);

    card = add<PanelContainer>(this);
    card->set_name("DialogPanel");
    card->set_theme_type_variation("CrimsonDialogPanel");
    auto *content = add<VBoxContainer>(card);
    content->add_theme_constant_override("separation", pixels(this, spacing::md));
    heading = label(content, {});
    heading->set_theme_type_variation("CrimsonDialogTitle");
    message = label(content, {});
    message->set_theme_type_variation("CrimsonDialogDescription");
    for (auto *copy : {heading, message}) {
        copy->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        copy->set_mouse_filter(MOUSE_FILTER_IGNORE);
    }

    form = add<VBoxContainer>(content);
    form->set_name("DialogBody");
    form->add_theme_constant_override("separation", pixels(this, spacing::md));
    validation_message = label(content, {});
    validation_message->set_name("DialogValidationMessage");
    validation_message->set_theme_type_variation("CrimsonDialogValidation");
    validation_message->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    validation_message->set_mouse_filter(MOUSE_FILTER_IGNORE);
    validation_message->hide();

    auto *footer_gap = add<Control>(content);
    footer_gap->set_custom_minimum_size(Vector2(0, pixels(this, spacing::xs)));
    footer_gap->set_mouse_filter(MOUSE_FILTER_IGNORE);
    auto *actions = add<HBoxContainer>(content);
    actions->set_alignment(BoxContainer::ALIGNMENT_END);
    actions->add_theme_constant_override("separation", pixels(this, spacing::md));

    cancel_button = button(actions, "Cancel", {}, callable_mp(this, &EditorModal::cancel));
    cancel_button->set_name("CancelDialog");
    cancel_button->set_theme_type_variation("CrimsonDialogSecondary");
    alternate_button = button(actions, {}, {}, callable_mp(this, &EditorModal::choose_alternate));
    alternate_button->set_name("AlternateDialog");
    alternate_button->set_theme_type_variation("CrimsonDialogSecondary");
    alternate_button->hide();
    confirm_button = button(actions, "OK", {}, callable_mp(this, &EditorModal::confirm));
    confirm_button->set_name("ConfirmDialog");
    confirm_button->set_theme_type_variation("CrimsonDialogPrimary");
    for (auto *action : {cancel_button, alternate_button, confirm_button}) {
        action->set_custom_minimum_size(Vector2(pixels(this, 64), pixels(this, 24)));
    }
    connect("resized", callable_mp(this, &EditorModal::fit_card));
    card->connect("minimum_size_changed", callable_mp(this, &EditorModal::fit_card));
    owner->connect("visibility_changed", callable_mp(this, &EditorModal::owner_visibility_changed));
    hide();
    set_process_input(false);
}

void EditorModal::fit_card() {
    const real_t available_width = std::max<real_t>(1, get_size().x - 2 * pixels(this, spacing::xl));
    const real_t width = std::min<real_t>(pixels(this, preferred_width), available_width);
    const real_t copy_width = std::max<real_t>(1, width - 2 * pixels(this, destructive ? spacing::xxl : spacing::xl));
    for (auto *copy : {heading, message, validation_message}) {
        copy->set_size(Vector2(copy_width, copy->get_size().y));
    }
    card->set_size(Vector2(width, 0));
    card->set_position(((get_size() - card->get_size()) * 0.5).floor());
}

void EditorModal::open(const String &prompt, const String &description, Control *initial_focus, int logical_width) {
    if (!active) {
        const Node *owner = get_parent();
        for (int index = 0; index < owner->get_child_count(); ++index) {
            const auto *other = Object::cast_to<EditorModal>(owner->get_child(index));
            if (other && other != this && other->is_open()) {
                return;
            }
        }
        auto *previous = get_viewport()->gui_get_focus_owner();
        prior_control = ObjectID(previous ? previous->get_instance_id() : 0);
        prior_focus = workbench->get_focus_behavior_recursive();
        prior_mouse = workbench->get_mouse_behavior_recursive();
        workbench->set_focus_behavior_recursive(FOCUS_BEHAVIOR_DISABLED);
        workbench->set_mouse_behavior_recursive(MOUSE_BEHAVIOR_DISABLED);
    }
    heading->set_text(prompt);
    set_accessibility_name(prompt);
    message->set_text(description);
    message->set_visible(!description.is_empty());
    form->set_visible(form->get_child_count() > 0);
    set_validation_message({});
    preferred_width = std::max(240, logical_width);
    active = true;
    show();
    fit_card();
    set_process_input(true);
    auto *safe_focus = cancel_button->is_visible() ? cancel_button : confirm_button;
    auto *target_focus = initial_focus && is_ancestor_of(initial_focus) && initial_focus->is_visible_in_tree() ? initial_focus : safe_focus;
    if (auto *entry = Object::cast_to<LineEdit>(target_focus)) {
        entry->edit();
    } else {
        target_focus->grab_focus();
    }
}

VBoxContainer *EditorModal::body() const {
    return form;
}

void EditorModal::set_confirm_text(const String &text) {
    confirm_button->set_text(text);
}

void EditorModal::set_confirm_enabled(bool enabled) {
    disable(confirm_button, !enabled);
}

void EditorModal::set_destructive(bool enabled) {
    destructive = enabled;
    card->set_theme_type_variation(enabled ? "CrimsonDialogDestructivePanel" : "CrimsonDialogPanel");
    confirm_button->set_theme_type_variation(enabled ? "CrimsonDialogDanger" : "CrimsonDialogPrimary");
    for (auto *action : {cancel_button, alternate_button}) {
        action->set_theme_type_variation(enabled ? "CrimsonDialogDestructiveSecondary" : "CrimsonDialogSecondary");
    }
    for (auto *action : {cancel_button, alternate_button, confirm_button}) {
        action->set_custom_minimum_size(Vector2(pixels(this, 64), pixels(this, enabled ? 32 : 24)));
    }
}

void EditorModal::set_cancel_visible(bool visible) {
    cancel_button->set_visible(visible);
}

void EditorModal::set_alternate_action(const String &text, const Callable &action) {
    on_alternate = action;
    alternate_button->set_text(text);
    alternate_button->set_visible(!text.is_empty() && action.is_valid());
}

void EditorModal::set_validator(const Callable &validate) {
    validator = validate;
}

void EditorModal::set_validation_message(const String &text) {
    validation_message->set_text(text);
    validation_message->set_visible(!text.is_empty());
}

void EditorModal::register_text_enter(LineEdit *entry) {
    entry->set_keep_editing_on_text_submit(true);
    entry->connect("text_submitted", callable_mp(this, &EditorModal::text_submitted));
}

void EditorModal::owner_visibility_changed() {
    const auto *control = Object::cast_to<Control>(get_parent());
    const auto *window = Object::cast_to<Window>(get_parent());
    if ((control && !control->is_visible_in_tree()) || (window && !window->is_visible())) {
        cancel();
    }
}

void EditorModal::_input(const Ref<InputEvent> &event) {
    if (!active || event.is_null()) {
        return;
    }
    if (event->is_action_pressed("ui_close_dialog") || event->is_action_pressed("ui_cancel")) {
        get_viewport()->set_input_as_handled();
        cancel();
    } else if (event->is_action_pressed("ui_focus_next", false, true) || event->is_action_pressed("ui_focus_prev", false, true)) {
        get_viewport()->set_input_as_handled();
        move_focus(event->is_action_pressed("ui_focus_prev", false, true));
    }
}

void EditorModal::move_focus(bool backwards) {
    auto *current = get_viewport()->gui_get_focus_owner();
    if (current && is_ancestor_of(current)) {
        auto *next = backwards ? current->find_prev_valid_focus() : current->find_next_valid_focus();
        if (next && is_ancestor_of(next)) {
            next->grab_focus();
            return;
        }
    }
    auto *fallback = cancel_button->is_visible() ? cancel_button : confirm_button;
    fallback->grab_focus();
}

void EditorModal::finish(const Callable &reply, bool restore_focus) {
    if (!active) {
        return;
    }
    const Callable callback = reply;
    active = false;
    set_process_input(false);
    hide();
    workbench->set_focus_behavior_recursive(prior_focus);
    workbench->set_mouse_behavior_recursive(prior_mouse);
    const ObjectID return_to = prior_control;
    prior_control = ObjectID();
    if (restore_focus) {
        auto *previous = Object::cast_to<Control>(ObjectDB::get_instance(return_to));
        if (previous && previous->is_inside_tree() && previous->is_visible_in_tree() && previous->get_focus_mode_with_override() != FOCUS_NONE) {
            previous->grab_focus();
        }
    }
    if (callback.is_valid()) {
        callback.call();
    }
}

void EditorModal::confirm() {
    if (active && !confirm_button->is_disabled() && (!validator.is_valid() || static_cast<bool>(validator.call()))) {
        finish(on_confirmed, false);
    }
}

void EditorModal::choose_alternate() {
    finish(on_alternate, false);
}

void EditorModal::text_submitted(const String &) {
    confirm();
}

void EditorModal::cancel() {
    finish(on_canceled, true);
}

void EditorModal::close() {
    finish({}, true);
}

bool EditorModal::is_open() const {
    return active;
}

EditorModal *removal_confirmation(Window *owner, Control *background, const Callable &confirmed, const Callable &canceled) {
    auto *modal = editor_modal(owner, background, confirmed, canceled);
    modal->set_name("RemovalConfirmation");
    modal->get_node<PanelContainer>("DialogScrim")->set_name("RemovalScrim");
    modal->get_node<PanelContainer>("DialogPanel")->set_name("RemovalPanel");
    modal->find_child("CancelDialog", true, false)->set_name("CancelRemoval");
    modal->find_child("ConfirmDialog", true, false)->set_name("ConfirmRemoval");
    modal->set_confirm_text("Remove");
    modal->set_destructive(true);
    return modal;
}

EditorModal *editor_modal(Node *owner, Control *background, const Callable &confirmed, const Callable &canceled) {
    auto *modal = add<EditorModal>(owner);
    modal->configure(owner, background, confirmed, canceled);
    return modal;
}

void style_file_dialog(FileDialog *dialog) {
    dialog->set_force_native(EditorInterface::get_singleton()->is_multi_window_enabled());
    const double scale = dialog->get_theme_default_base_scale();
    // Cover the full native window, including where a rounded card would reveal its clear color.
    const auto panel = box(color(Surface::HEADER), color(Surface::OUTLINE), spacing::lg, spacing::lg, scale);
    Ref<Theme> file_theme;
    file_theme.instantiate();
    file_theme->set_stylebox("panel", "AcceptDialog", panel);
    file_theme->set_constant("buttons_min_height", "AcceptDialog", std::lround(24 * scale));
    dialog->set_theme(file_theme);
    dialog->get_ok_button()->set_theme_type_variation("CrimsonDialogPrimary");
    dialog->get_cancel_button()->set_theme_type_variation("CrimsonDialogSecondary");
}

void embed_editor_popups(Window *owner) {
    // Native popup transparency can depend on the project renderer. The tool can always composite its own child windows.
    owner->set_embedding_subwindows(true);
}

void tooltip(Control *control, const String &help) {
    if (help.is_empty()) {
        return;
    }
    const PackedStringArray words = help.split(" ", false);
    String display;
    int length = 0;
    for (const String &word : words) {
        if (length > 0 && length + word.length() > 62) {
            display += "\n";
            length = 0;
        } else if (length > 0) {
            display += " ";
            ++length;
        }
        display += word;
        length += word.length();
    }
    control->set_tooltip_text(display);
    control->set_accessibility_description(help);
}

ProgressBar *progress(Node *owner, const String &name, double percent, bool indeterminate) {
    auto *row = add<HBoxContainer>(owner);
    row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    auto *control = add<ProgressBar>(row);
    control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    control->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    control->set_custom_minimum_size(Vector2(pixels(owner, 100), pixels(owner, 6)));
    control->set_show_percentage(false);
    control->set_accessibility_name(name);
    control->set_indeterminate(indeterminate);
    control->set_value(percent);
    if (!indeterminate) {
        auto *readout = label(row, String::num_int64(std::lround(percent)) + "%");
        readout->set_custom_minimum_size(Vector2(pixels(owner, 40), 0));
        readout->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
        control->connect("value_changed", callable_mp_static(+[](double current, Label *value) {
            value->set_text(String::num_int64(std::lround(current)) + "%");
        }).bind(readout));
    }
    return control;
}

void register_editor_controls() {
    GDREGISTER_INTERNAL_CLASS(ShortcutTooltipButton);
    GDREGISTER_INTERNAL_CLASS(NumberInput);
    GDREGISTER_INTERNAL_CLASS(Picker);
    GDREGISTER_INTERNAL_CLASS(EditorModal);
}

} // namespace crimson::editor_ui

#endif
