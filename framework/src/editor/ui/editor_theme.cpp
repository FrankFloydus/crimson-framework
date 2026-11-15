// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "editor_controls.hpp"

#ifdef DEBUG_ENABLED

#include "editor_spacing.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/window.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::editor_ui {
using namespace godot;

namespace {

/// Convert the color to its SVG representation.
String css(Color ink) {
    return "#" + ink.to_html(false);
}

/// Match highlight ink across native command rows and shared picker rows.
Color popup_row_highlight() {
    return Color::html("#313234");
}

/// Draw a pixel-scaled image from a supplied SVG path or shape.
Ref<Texture2D> svg(const String &body, double scale, int width = 16, int height = 16) {
    const String size = " width=\"" + String::num_int64(width) + "\" height=\"" + String::num_int64(height) + "\"";
    Ref<Image> image;
    image.instantiate();
    if (image->load_svg_from_string("<svg xmlns=\"http://www.w3.org/2000/svg\"" + size + ">" + body + "</svg>", scale) != OK) {
        return {};
    }
    return ImageTexture::create_from_image(image);
}

/// Draw a compact line icon at the editor scale.
Ref<Texture2D> line_icon(const String &path, double scale, Color ink, const String &stroke_width = "1.2") {
    return svg("<g fill=\"none\" stroke=\"" + css(ink) + "\" stroke-width=\"" + stroke_width +
            "\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" + path + "</g>", scale);
}

/// Draw distinct checkbox, radio and mixed markers at the same size.
Ref<Texture2D> choice_icon(double scale, bool selected, bool radio, bool disabled, bool mixed = false, bool hovered = false) {
    const bool active = selected || mixed;
    const Color outline = hovered && !disabled ? Color::html("#6975e2") :
            color(disabled ? Surface::RULE : active ? Surface::ACCENT : Surface::DISABLED);
    const String fill = !radio && !active ? String("none") : css(color(active && !disabled ? Surface::ACCENT : Surface::HEADER));
    const Color mark = color(disabled ? Surface::DISABLED : Surface::TEXT);
    String content = radio ? "<circle cx=\"7\" cy=\"7\" r=\"6.5\"" : "<rect x=\".5\" y=\".5\" width=\"13\" height=\"13\" rx=\"3\"";
    content += " fill=\"" + fill + "\" stroke=\"" + css(outline) + "\"/>";
    if (mixed) {
        content += "<path d=\"M3.5 7h7\" stroke=\"" + css(mark) + "\"/>";
    } else if (selected) {
        content += radio ? "<circle cx=\"7\" cy=\"7\" r=\"2.6\" fill=\"" + css(mark) + "\"/>" :
                "<path d=\"m3.5 7 2.2 2.2 4.8-4.7\" fill=\"none\" stroke=\"" + css(mark) +
                        "\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>";
    }
    return svg(content, scale, 14, 14);
}

/// Draw a compact switch rail and thumb, including mirrored text direction.
Ref<Texture2D> switch_icon(double scale, bool selected, bool disabled, bool mirrored) {
    const Color fill = disabled ? color(Surface::RULE) : selected ? color(Surface::ACCENT) : Color::html("#727374");
    const Color mark = disabled ? color(Surface::DISABLED) : Color::html("#fffeff");
    const String content = "<rect width=\"22\" height=\"14\" rx=\"7\" fill=\"" + css(fill) +
            "\"/><circle cx=\"" + String(selected != mirrored ? "15" : "7") + "\" cy=\"7\" r=\"5\" fill=\"" + css(mark) + "\"/>";
    return svg(content, scale, 22, 14);
}

/// Separate default, hovered and selected button chrome.
void button_styles(const Ref<Theme> &theme, const StringName &type, double scale, ButtonStyle style,
        int corner = control_corner_radius) {
    const Color none(0, 0, 0, 0);
    const bool primary = style == ButtonStyle::PRIMARY;
    const bool quiet = style == ButtonStyle::QUIET;
    const bool row = style == ButtonStyle::ROW;
    const int inset = quiet ? 5 : row ? spacing::md : 10;
    const Color ink = primary ? Color::html("#ffffff") : color(style == ButtonStyle::DESTRUCTIVE ? Surface::DANGER : Surface::TEXT);
    const Color normal = primary ? color(Surface::ACCENT) : quiet || row ? none : color(Surface::RAISED);
    const Color stroke = primary || quiet || row ? none : color(Surface::OUTLINE);
    const Color highlight = primary ? normal.lightened(0.045f) : color(Surface::HOVER);
    theme->set_font_size("font_size", type, std::lround(12 * scale));
    for (const char *state : {"font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color", "font_focus_color"}) {
        theme->set_color(state, type, ink);
    }
    theme->set_color("font_disabled_color", type, color(Surface::DISABLED));
    theme->set_stylebox("normal", type, rounded_box(normal, stroke, inset, 4, scale, corner));
    theme->set_stylebox("hover", type, rounded_box(highlight, stroke, inset, 4, scale, corner));
    const auto selected = rounded_box(primary ? normal.darkened(0.10f) : color(Surface::SELECTED), stroke, inset, 4, scale, corner);
    theme->set_stylebox("pressed", type, selected);
    theme->set_stylebox("hover_pressed", type, selected);
    theme->set_stylebox("disabled", type, rounded_box(quiet || row ? none : color(Surface::HEADER),
            quiet || row ? none : color(Surface::RULE), inset, 4, scale, corner));
    theme->set_stylebox("focus", type, box(none, none));
}

/// Configure weight and optical size from the single bundled Inter variable font.
Ref<FontVariation> inter_variation(const Ref<FontFile> &inter, int weight, int optical_size) {
    Ref<FontVariation> variation;
    variation.instantiate();
    variation->set_base_font(inter);
    Dictionary axes;
    // OpenType tags are the four axis-name bytes packed in big-endian order.
    axes[0x77676874] = weight; // wght.
    axes[0x6f70737a] = optical_size; // opsz.
    variation->set_variation_opentype(axes);
    return variation;
}

/// Give the editor a consistent compact text rhythm using its bundled Inter.
void text_styles(const Ref<Theme> &theme, double scale) {
    theme->set_default_font_size(std::lround(13 * scale));
    theme->set_color("font_color", "Label", color(Surface::TEXT));
    for (const char *type : {"CrimsonMuted", "CrimsonHeading", "CrimsonTitle"}) {
        theme->set_type_variation(type, "Label");
    }
    theme->set_font_size("font_size", "CrimsonMuted", std::lround(11 * scale));
    theme->set_color("font_color", "CrimsonMuted", color(Surface::MUTED));
    theme->set_font_size("font_size", "CrimsonHeading", std::lround(12 * scale));
    theme->set_font_size("font_size", "CrimsonTitle", std::lround(16 * scale));

    Ref<FontFile> inter;
    inter.instantiate();
    if (inter->load_dynamic_font("res://addons/crimson-framework/editor/fonts/InterVariable.ttf") == OK) {
        theme->set_default_font(inter_variation(inter, 450, 14));
        const auto medium = inter_variation(inter, 550, 14);
        const auto compact = inter_variation(inter, 400, 14);
        theme->set_font("font", "CrimsonHeading", medium);
        theme->set_font("font", "CrimsonPrimary", inter_variation(inter, 500, 14));
        theme->set_font("font", "CrimsonSelect", compact);
        theme->set_font("font", "CrimsonShortcutKey", compact);
        theme->set_font("font", "CrimsonTitle", inter_variation(inter, 600, 28));
        const auto dialog_heading = inter_variation(inter, 600, 14);
        const auto dialog_action = inter_variation(inter, 500, 14);
        theme->set_font("font", "CrimsonDialogTitle", dialog_heading);
        theme->set_font("font", "CrimsonDialogDanger", dialog_heading);
        theme->set_font("font", "CrimsonDialogDestructiveSecondary", dialog_heading);
        theme->set_font("font", "CrimsonDialogPrimary", dialog_action);
        theme->set_font("font", "CrimsonDialogSecondary", dialog_action);
        const auto tab = inter_variation(inter, 510, 14);
        theme->set_font("font", "TabBar", tab);
        theme->set_font("font", "TabContainer", tab);
    }
}

/// Theme both text inputs and their non-editable and invalid states.
void field_styles(const Ref<Theme> &theme, double scale) {
    const Color none(0, 0, 0, 0);
    const Color header = color(Surface::HEADER);
    const Color ink = color(Surface::TEXT);
    const auto frame = rounded_box(header, color(Surface::OUTLINE), 9, 4, scale);
    const auto readonly = rounded_box(color(Surface::SIDEBAR), color(Surface::RULE), 9, 4, scale);
    const auto transparent = box(none, none);

    for (const char *type : {"LineEdit", "TextEdit", "CodeEdit"}) {
        theme->set_font_size("font_size", type, std::lround(12 * scale));
        theme->set_stylebox("normal", type, frame);
        theme->set_stylebox("read_only", type, readonly);
        theme->set_stylebox("focus", type, transparent);
        theme->set_color("font_color", type, ink);
        theme->set_color("font_uneditable_color", type, ink);
        theme->set_color("font_readonly_color", type, ink);
        theme->set_color("font_placeholder_color", type, color(Surface::MUTED));
        theme->set_color("caret_color", type, ink);
        theme->set_color("selection_color", type, color(Surface::ACCENT));
    }
    theme->set_type_variation("CrimsonPopupSearch", "LineEdit");
    theme->set_font_size("font_size", "CrimsonPopupSearch", std::lround(13 * scale));
    theme->set_stylebox("normal", "CrimsonPopupSearch", box(none, none, 9, 4, scale));
    theme->set_stylebox("focus", "CrimsonPopupSearch", transparent);
}

/// Share label and indicator states across native choice controls.
void check_styles(const Ref<Theme> &theme, double scale) {
    const Color none(0, 0, 0, 0);
    const auto clean = box(none, none, 0, 0, scale);
    for (const char *type : {"CheckBox", "CheckButton"}) {
        for (const char *state : {"normal", "pressed", "hover", "hover_pressed", "disabled"}) {
            theme->set_stylebox(state, type, clean);
        }
        theme->set_stylebox("focus", type, clean);
        theme->set_constant("h_separation", type, std::lround(7 * scale));
        theme->set_color("font_color", type, color(Surface::TEXT));
        theme->set_color("font_hover_color", type, color(Surface::TEXT));
        theme->set_color("font_pressed_color", type, color(Surface::TEXT));
        theme->set_color("font_disabled_color", type, color(Surface::DISABLED));
    }
    theme->set_type_variation("CrimsonMixed", "CheckBox");
    theme->set_type_variation("CrimsonCheckboxHover", "CheckBox");
    theme->set_type_variation("CrimsonMixedHover", "CheckBox");
    theme->set_icon("unchecked", "CrimsonMixed", choice_icon(scale, false, false, false, true));
    theme->set_icon("unchecked", "CrimsonMixedHover", choice_icon(scale, false, false, false, true, true));
    for (const bool selected : {false, true}) {
        theme->set_icon(selected ? "checked" : "unchecked", "CrimsonCheckboxHover",
                choice_icon(scale, selected, false, false, false, true));
        for (const bool disabled : {false, true}) {
            const String key = String(selected ? "checked" : "unchecked") + (disabled ? "_disabled" : "");
            theme->set_icon(key, "CheckBox", choice_icon(scale, selected, false, disabled));
            theme->set_icon("radio_" + key, "CheckBox", choice_icon(scale, selected, true, disabled));
            theme->set_icon(key, "CheckButton", switch_icon(scale, selected, disabled, false));
            theme->set_icon(key + String("_mirrored"), "CheckButton", switch_icon(scale, selected, disabled, true));
        }
    }
}

/// Theme narrow slider and progress rails with a rounded thumb.
void range_styles(const Ref<Theme> &theme, double scale) {
    const Color none(0, 0, 0, 0);
    const auto track = rounded_box(color(Surface::RULE), none, 0, 2, scale, 2);
    const auto fill = rounded_box(color(Surface::HIGHLIGHT), none, 0, 2, scale, 2);
    theme->set_stylebox("slider", "HSlider", track);
    theme->set_stylebox("grabber_area", "HSlider", fill);
    theme->set_stylebox("grabber_area_highlight", "HSlider", fill);
    theme->set_stylebox("focus", "HSlider", box(none, none));
    for (const char *state : {"grabber", "grabber_highlight", "grabber_disabled"}) {
        const Color ink = color(String(state) == "grabber_disabled" ? Surface::DISABLED : Surface::TEXT);
        theme->set_icon(state, "HSlider", svg("<circle cx=\"6\" cy=\"6\" r=\"4.8\" fill=\"" +
                css(ink) + "\" stroke=\"" + css(color(Surface::CANVAS)) + "\"/>", scale, 12, 12));
    }
    theme->set_stylebox("background", "ProgressBar", track);
    theme->set_stylebox("fill", "ProgressBar", fill);
    theme->set_color("font_color", "ProgressBar", color(Surface::TEXT));
    theme->set_font_size("font_size", "ProgressBar", std::lround(12 * scale));
}

/// Theme panes, popups, tabs and split handles.
void container_styles(const Ref<Theme> &theme, double scale, bool composite_popups) {
    const Color none(0, 0, 0, 0);
    const Color ink = color(Surface::TEXT);
    const Color muted = color(Surface::MUTED);
    const auto canvas = box(color(Surface::HEADER), none, 0, 0, scale);
    const Color popup_edge = Color::html("#323336");
    const Color popup_results = Color::html("#1f1f20");
    // An opaque native window would show its clear color wherever a rounded panel leaves gaps.
    const int window_corner = composite_popups ? popup_corner_radius : 0;
    const auto overlay = rounded_box(Color::html("#212122"), popup_edge,
            spacing::sm, spacing::sm, scale, window_corner);
    const auto menu = rounded_box(popup_results, popup_edge,
            spacing::sm, spacing::sm, scale, window_corner);
    const auto transparent = box(none, none);
    theme->set_stylebox("panel", "PanelContainer", canvas);
    theme->set_stylebox("separator", "HSeparator", box(color(Surface::RULE), none, 0, 0, scale));
    theme->set_constant("separation", "HSeparator", std::lround(scale));
    theme->set_stylebox("panel", "PopupPanel", overlay);
    theme->set_stylebox("panel", "PopupMenu", menu);
    theme->set_stylebox("panel", "CrimsonOverlayMenu", rounded_box(popup_results, popup_edge,
            spacing::sm, spacing::sm, scale, popup_corner_radius));
    theme->set_type_variation("CrimsonMenuPopup", "PopupPanel");
    theme->set_stylebox("panel", "CrimsonMenuPopup", menu);
    theme->set_type_variation("CrimsonSelectPopup", "PopupPanel");
    const auto select_popup = rounded_box(Color::html("#28292b"), none, 0, spacing::xs, scale, window_corner);
    select_popup->set_shadow_color(Color(0, 0, 0, 0.30));
    select_popup->set_shadow_size(composite_popups ? std::lround(scale) : 0);
    theme->set_stylebox("panel", "CrimsonSelectPopup", select_popup);
    theme->set_type_variation("CrimsonPickerResults", "ScrollContainer");
    theme->set_stylebox("panel", "CrimsonPickerResults", box(popup_results, none, 0, 0, scale));
    // Stock dialogs have an opaque content viewport, including when Godot embeds the dialog window.
    theme->set_stylebox("panel", "AcceptDialog", box(color(Surface::HEADER), color(Surface::OUTLINE), 12, 12, scale));
    theme->set_stylebox("hover", "PopupMenu", rounded_box(popup_row_highlight(), none, 0, 0, scale));
    const auto divider = box(popup_edge, none, 0, 0, scale);
    divider->set_content_margin(SIDE_TOP, scale);
    for (const char *type : {"separator", "labeled_separator_left", "labeled_separator_right"}) {
        theme->set_stylebox(type, "PopupMenu", divider);
    }
    theme->set_color("font_color", "PopupMenu", Color::html("#e5e6e8"));
    theme->set_color("font_hover_color", "PopupMenu", Color::html("#ffffff"));
    theme->set_color("font_disabled_color", "PopupMenu", color(Surface::DISABLED));
    theme->set_color("font_accelerator_color", "PopupMenu", muted);
    theme->set_font_size("font_size", "PopupMenu", std::lround(13 * scale));
    theme->set_color("font_separator_color", "PopupMenu", muted);
    theme->set_font_size("font_separator_size", "PopupMenu", std::lround(11 * scale));
    theme->set_constant("v_separation", "PopupMenu", std::lround(14 * scale));
    theme->set_constant("h_separation", "PopupMenu", std::lround(spacing::md * scale));
    theme->set_constant("item_start_padding", "PopupMenu", std::lround(spacing::md * scale));
    theme->set_constant("item_end_padding", "PopupMenu", std::lround(spacing::md * scale));
    theme->set_constant("search_bar_separation", "PopupMenu", std::lround(4 * scale));
    const auto tooltip_panel = rounded_box(Color::html("#212122"), popup_edge, 9, 6, scale,
            composite_popups ? control_corner_radius : 0);
    // Give Inter's 15px native line the 18px line box used in the reference.
    const auto tooltip_label = box(none, none, 0, 0, scale);
    tooltip_label->set_content_margin(SIDE_TOP, 1.5 * scale);
    tooltip_label->set_content_margin(SIDE_BOTTOM, 1.5 * scale);
    tooltip_panel->set_shadow_color(Color(0, 0, 0, 0.30));
    tooltip_panel->set_shadow_size(composite_popups ? std::lround(2 * scale) : 0);
    tooltip_panel->set_shadow_offset(Vector2(0, 0.5 * scale));
    theme->set_stylebox("panel", "TooltipPanel", tooltip_panel);
    theme->set_stylebox("normal", "TooltipLabel", tooltip_label);
    theme->set_color("font_color", "TooltipLabel", Color::html("#e5e6e8"));
    theme->set_font_size("font_size", "TooltipLabel", std::lround(12 * scale));
    theme->set_type_variation("CrimsonShortcutKey", "Label");
    theme->set_stylebox("normal", "CrimsonShortcutKey", rounded_box(none, Color::html("#292a2d"), 3, 2, scale, 4));
    theme->set_color("font_color", "CrimsonShortcutKey", Color::html("#9c9d9f"));
    theme->set_font_size("font_size", "CrimsonShortcutKey", std::lround(12 * scale));
    theme->set_stylebox("panel", "TabContainer", canvas);
    const auto baseline = box(none, none, 0, 0, scale);
    baseline->set_border_width(SIDE_BOTTOM, std::lround(scale));
    baseline->set_border_color(Color::html("#23252a"));
    theme->set_stylebox("tabbar_background", "TabContainer", baseline);
    const auto tab = box(none, none, 0, 12, scale);
    const auto active_tab = box(none, none, 0, 12, scale);
    const Color active_tab_ink = Color::html("#f7f8f8");
    active_tab->set_border_width(SIDE_BOTTOM, std::lround(scale));
    active_tab->set_border_color(active_tab_ink);
    theme->set_constant("side_margin", "TabContainer", 0);
    for (const char *type : {"TabBar", "TabContainer"}) {
        theme->set_stylebox("tab_selected", type, active_tab);
        theme->set_stylebox("tab_unselected", type, tab);
        theme->set_stylebox("tab_hovered", type, tab);
        theme->set_stylebox("tab_disabled", type, tab);
        theme->set_stylebox("tab_focus", type, transparent);
        theme->set_color("font_selected_color", type, active_tab_ink);
        theme->set_color("font_hovered_color", type, Color::html("#d0d6e0"));
        theme->set_color("font_unselected_color", type, Color::html("#8a8f98"));
        theme->set_color("font_disabled_color", type, color(Surface::DISABLED));
        theme->set_font_size("font_size", type, std::lround(14 * scale));
        theme->set_constant("tab_separation", type, std::lround(24 * scale));
    }
    for (const char *type : {"VBoxContainer", "HBoxContainer"}) {
        theme->set_constant("separation", type, std::lround(4 * scale));
    }
    for (const char *type : {"HSplitContainer", "VSplitContainer", "SplitContainer"}) {
        theme->set_constant("separation", type, std::lround(7 * scale));
        theme->set_constant("minimum_grab_thickness", type, std::lround(7 * scale));
        theme->set_constant("autohide", type, 0);
        theme->set_stylebox("focus", type, transparent);
        theme->set_stylebox("split_bar_background", type, box(color(Surface::CANVAS), none, 0, 0, scale));
        theme->set_icon("h_grabber", type, svg("<path d=\"M3.5 3v12\" stroke=\"" +
                css(color(Surface::DISABLED)) + "\" stroke-linecap=\"round\"/>", scale, 7, 18));
        theme->set_icon("v_grabber", type, svg("<path d=\"M3 3.5h12\" stroke=\"" +
                css(color(Surface::DISABLED)) + "\" stroke-linecap=\"round\"/>", scale, 18, 7));
    }
}

/// Style native lists, trees and scrollbars used by tool inspectors and command menus.
void list_styles(const Ref<Theme> &theme, double scale) {
    const Color none(0, 0, 0, 0);
    const auto flat = box(none, none, 2, 1, scale);
    const auto selection = rounded_box(color(Surface::SELECTED), none, 5, 2, scale);
    const auto hovered = rounded_box(color(Surface::HOVER), none, 5, 2, scale);
    const auto transparent = box(none, none);
    for (const char *type : {"Tree", "ItemList"}) {
        theme->set_stylebox("panel", type, flat);
        theme->set_stylebox("selected", type, selection);
        theme->set_stylebox("selected_focus", type, selection);
        theme->set_stylebox("hovered_selected", type, hovered);
        theme->set_stylebox("hovered_selected_focus", type, hovered);
        theme->set_stylebox("focus", type, transparent);
        theme->set_stylebox("cursor", type, transparent);
        theme->set_stylebox("cursor_unfocused", type, transparent);
        theme->set_stylebox("hovered", type, hovered);
        theme->set_color("font_color", type, color(Surface::TEXT));
        theme->set_color("font_selected_color", type, color(Surface::TEXT));
        theme->set_font_size("font_size", type, std::lround(12 * scale));
        theme->set_constant("v_separation", type, std::lround(4 * scale));
    }
    theme->set_stylebox("title_button_normal", "Tree", box(color(Surface::SIDEBAR), none, 6, 4, scale));
    theme->set_stylebox("title_button_hover", "Tree", hovered);
    theme->set_stylebox("title_button_pressed", "Tree", selection);
    theme->set_color("title_button_color", "Tree", color(Surface::MUTED));
    theme->set_font_size("title_button_font_size", "Tree", std::lround(11 * scale));
    for (const char *type : {"HScrollBar", "VScrollBar"}) {
        const auto grabber = rounded_box(color(Surface::OUTLINE), none, 3, 3, scale, 3);
        const auto grabber_hot = rounded_box(color(Surface::DISABLED), none, 3, 3, scale, 3);
        const auto track = box(none, none, 0, 0, scale);
        theme->set_stylebox("scroll", type, track);
        theme->set_stylebox("scroll_focus", type, track);
        theme->set_stylebox("grabber", type, grabber);
        theme->set_stylebox("grabber_highlight", type, grabber_hot);
        theme->set_stylebox("grabber_pressed", type, grabber_hot);
    }
}

/// Style authored forms and destructive confirmations within the workbench.
void dialog_styles(const Ref<Theme> &theme, double scale) {
    const Color background = Color::html("#1a1a1b");
    const Color outline = Color::html("#2b2c2e");
    const Color none(0, 0, 0, 0);
    theme->set_type_variation("CrimsonDialogPanel", "PanelContainer");
    theme->set_type_variation("CrimsonDialogDestructivePanel", "PanelContainer");
    theme->set_type_variation("CrimsonDialogScrim", "PanelContainer");
    theme->set_type_variation("CrimsonDialogTitle", "Label");
    theme->set_type_variation("CrimsonDialogDescription", "Label");
    theme->set_type_variation("CrimsonDialogValidation", "Label");
    theme->set_stylebox("panel", "CrimsonDialogPanel", rounded_box(background, outline,
            spacing::xl, spacing::xl, scale, popup_corner_radius));
    theme->set_stylebox("panel", "CrimsonDialogDestructivePanel", rounded_box(background, outline,
            spacing::xxl, spacing::xxl, scale, popup_corner_radius));
    theme->set_stylebox("panel", "CrimsonDialogScrim", box(Color(0, 0, 0, 0.4), none));
    theme->set_color("font_color", "CrimsonDialogTitle", Color::html("#ffffff"));
    theme->set_font_size("font_size", "CrimsonDialogTitle", std::lround(15 * scale));
    theme->set_color("font_color", "CrimsonDialogDescription", Color::html("#959597"));
    theme->set_font_size("font_size", "CrimsonDialogDescription", std::lround(15 * scale));
    theme->set_color("font_color", "CrimsonDialogValidation", color(Surface::DANGER));
    theme->set_font_size("font_size", "CrimsonDialogValidation", std::lround(12 * scale));

    struct ActionStyle {
        const char *type;
        const char *fill;
        bool primary;
        bool dense;
    };
    for (const auto &[type, fill, primary, dense] : {
            ActionStyle{"CrimsonDialogPrimary", "#5e6ad2", true, true},
            ActionStyle{"CrimsonDialogSecondary", "#232325", false, true},
            ActionStyle{"CrimsonDialogDanger", "#f34e52", true, false},
            ActionStyle{"CrimsonDialogDestructiveSecondary", "#232325", false, false}}) {
        const int radius = dense ? 12 : 16;
        const int horizontal_padding = dense ? 8 : 12;
        const int vertical_padding = dense ? 3 : 4;
        theme->set_type_variation(type, "Button");
        button_styles(theme, type, scale, primary ? ButtonStyle::PRIMARY : ButtonStyle::SECONDARY, radius);
        const Color idle = Color::html(fill);
        const Color edge = primary ? none : outline;
        theme->set_stylebox("normal", type, rounded_box(idle, edge, horizontal_padding, vertical_padding, scale, radius));
        theme->set_stylebox("hover", type, rounded_box(idle.lightened(0.045f), edge, horizontal_padding, vertical_padding, scale, radius));
        const auto selected = rounded_box(idle.darkened(0.10f), edge, horizontal_padding, vertical_padding, scale, radius);
        theme->set_stylebox("pressed", type, selected);
        theme->set_stylebox("hover_pressed", type, selected);
        theme->set_font_size("font_size", type, std::lround((dense ? 12 : 13) * scale));
    }
}

/// Register the compact editor icons as theme textures.
void icons(const Ref<Theme> &theme, double scale) {
    const struct { const char *name; const char *shape; } artwork[] = {
        {"add", "<path d=\"M8 3v10M3 8h10\"/>"},
        {"add_circle", "<circle cx=\"8\" cy=\"8\" r=\"6\"/><path d=\"M8 5v6M5 8h6\"/>"},
        {"minus", "<path d=\"M3 8h10\"/>"},
        {"remove", "<path d=\"M3 4h10m-7-1.5h4m-5.8 2 1 9h5.6l1-9M7 6.5v4.5m2-4.5v4.5\"/>"},
        {"grip", "<path d=\"M6 4h.1m3.8 0h.1M6 8h.1m3.8 0h.1M6 12h.1m3.8 0h.1\"/>"},
        {"close", "<path d=\"m4 4 8 8m0-8-8 8\"/>"},
        {"check", "<path d=\"m4 8 2.5 2.5L12 5\"/>"},
        {"chevron_down", "<path d=\"m5 6 3 3 3-3\"/>"},
        {"chevron_up", "<path d=\"m5 10 3-3 3 3\"/>"},
        {"chevron_left", "<path d=\"m10 4.5-3.5 3.5 3.5 3.5\"/>"},
        {"chevron_right", "<path d=\"m6 4.5 3.5 3.5L6 11.5\"/>"},
        {"search", "<circle cx=\"6.7\" cy=\"6.7\" r=\"3.7\"/><path d=\"m9.5 9.5 3.5 3.5\"/>"},
        {"eye", "<path d=\"M1.7 8s2.2-4 6.3-4 6.3 4 6.3 4-2.2 4-6.3 4-6.3-4-6.3-4Z\"/><circle cx=\"8\" cy=\"8\" r=\"1.5\"/>"},
        {"eye_off", "<path d=\"M1.7 8s2.2-4 6.3-4a7.1 7.1 0 0 1 6.3 4 8.9 8.9 0 0 1-1.7 2.1M10.5 11.6A7.2 7.2 0 0 1 1.7 8M3 3l10 10\"/>"},
        {"link", "<path d=\"m6 10 4-4m-4-1 1.8-1.8a2.8 2.8 0 0 1 4 4L10 9m0 2-1.8 1.8a2.8 2.8 0 0 1-4-4L6 7\"/>"},
        {"axes", "<path d=\"M3 3v10h10m-8-2 3-4 2 2 3-4\"/>"},
        {"keyboard", "<rect x=\"2\" y=\"4\" width=\"12\" height=\"8\" rx=\"1\"/><path d=\"M4 6h1m2 0h1m2 0h1m-7 2h1m2 0h1m2 0h1m-6 2h6\"/>"},
        {"mouse", "<rect x=\"4.5\" y=\"2\" width=\"7\" height=\"12\" rx=\"3.5\"/><path d=\"M8 3.5v3\"/>"},
        {"gamepad", "<path d=\"M5.4 4.5h5.2c1.7 0 2.5 1 2.8 2.5l.6 3.1c.2 1.4-1 1.9-1.9 1L10 9.4H6L3.9 11c-.9.9-2.1.4-1.9-1L2.6 7c.3-1.5 1.1-2.5 2.8-2.5Z\"/><path d=\"M5.4 6v2.7M4 7.35h2.7m3.2-.5h.5m1 1h.5\"/>"},
        {"folder", "<path d=\"M2.5 5V3.7h4l1.4 1.7h5.6v6.8h-11Z\"/>"},
        {"file", "<path d=\"M4.5 2.5H9l2.5 2.6v8.4h-7Zm4.5 0v3h2.5\"/>"}
    };
    for (const auto &entry : artwork) {
        theme->set_icon(entry.name, "CrimsonIcons", line_icon(entry.shape, scale, color(Surface::MUTED)));
        const StringName name(entry.name);
        if (name == StringName("keyboard") || name == StringName("mouse") || name == StringName("gamepad") ||
                name == StringName("link") || name == StringName("add") || name == StringName("eye")) {
            theme->set_icon(name, "CrimsonHighlightedIcons", line_icon(entry.shape, scale, color(Surface::HIGHLIGHT), "1.8"));
        }
    }
    theme->set_icon("selected_check", "CrimsonIcons", line_icon("<path d=\"m4 8 2.5 2.5L12 5\"/>", scale, color(Surface::HIGHLIGHT)));
    theme->set_icon("arrow", "OptionButton", theme->get_icon("chevron_down", "CrimsonIcons"));
    theme->set_icon("submenu", "PopupMenu", theme->get_icon("chevron_right", "CrimsonIcons"));
    theme->set_icon("submenu_mirrored", "PopupMenu", theme->get_icon("chevron_left", "CrimsonIcons"));
    theme->set_icon("search", "PopupMenu", theme->get_icon("search", "CrimsonIcons"));
    theme->set_icon("clear", "LineEdit", theme->get_icon("close", "CrimsonIcons"));
}

} // namespace

Color color(Surface role) {
    switch (role) {
        case Surface::CANVAS: return Color::html("#0f0f11");
        case Surface::HEADER: return Color::html("#19191b");
        case Surface::RAISED: return Color::html("#1e1f21");
        case Surface::RULE: return Color::html("#252527");
        case Surface::OUTLINE: return Color::html("#363639");
        case Surface::TEXT: return Color::html("#e8e7ea");
        case Surface::MUTED: return Color::html("#a0a0a6");
        case Surface::HOVER: return Color::html("#232326");
        case Surface::SELECTED: return Color::html("#232325");
        case Surface::HIGHLIGHT: return Color::html("#a89dfa");
        case Surface::DANGER: return Color::html("#ee7186");
        case Surface::SIDEBAR: return Color::html("#121213");
        case Surface::ACCENT: return Color::html("#5e6ad2");
        case Surface::DISABLED: return Color::html("#76767d");
    }
    return {};
}

Ref<StyleBoxFlat> box(Color background, Color outline, int inset_x, int inset_y, double scale) {
    Ref<StyleBoxFlat> result;
    result.instantiate();
    result->set_bg_color(background);
    result->set_border_color(outline);
    result->set_border_width_all(outline.a > 0.0 ? std::max(1, static_cast<int>(std::round(scale))) : 0);
    result->set_anti_aliased(false);
    result->set_content_margin(SIDE_LEFT, inset_x * scale);
    result->set_content_margin(SIDE_RIGHT, inset_x * scale);
    result->set_content_margin(SIDE_TOP, inset_y * scale);
    result->set_content_margin(SIDE_BOTTOM, inset_y * scale);
    return result;
}

Ref<StyleBoxFlat> rounded_box(Color background, Color outline, int inset_x, int inset_y, double scale, int corner) {
    auto style = box(background, outline, inset_x, inset_y, scale);
    style->set_corner_radius_all(std::lround(corner * scale));
    style->set_anti_aliased(true);
    return style;
}

Ref<Theme> create_theme(const Window *owner, double scale) {
    const bool composite_popups = (owner && (owner->is_embedding_subwindows() || owner->is_embedded())) ||
            DisplayServer::get_singleton()->is_window_transparency_available();
    scale = std::clamp(scale, 0.75, 4.0);
    Ref<Theme> theme;
    theme.instantiate();
    theme->set_default_base_scale(scale);
    text_styles(theme, scale);
    button_styles(theme, "Button", scale, ButtonStyle::SECONDARY);
    theme->set_type_variation("CrimsonPrimary", "Button");
    theme->set_type_variation("CrimsonQuiet", "Button");
    theme->set_type_variation("CrimsonDanger", "Button");
    theme->set_type_variation("CrimsonRow", "Button");
    theme->set_type_variation("CrimsonIcon", "Button");
    theme->set_type_variation("CrimsonQuietIcon", "Button");
    theme->set_type_variation("CrimsonDangerIcon", "Button");
    theme->set_type_variation("CrimsonSelect", "Button");
    theme->set_type_variation("CrimsonPickerOption", "CrimsonRow");
    theme->set_type_variation("CrimsonCompactSelectOption", "Button");
    theme->set_type_variation("CrimsonCompactSelectCandidate", "CrimsonCompactSelectOption");
    theme->set_type_variation("CrimsonMenuSelectOption", "CrimsonCompactSelectOption");
    theme->set_type_variation("CrimsonMenuSelectCandidate", "CrimsonMenuSelectOption");
    button_styles(theme, "CrimsonPrimary", scale, ButtonStyle::PRIMARY, 14);
    button_styles(theme, "CrimsonQuiet", scale, ButtonStyle::QUIET);
    button_styles(theme, "CrimsonDanger", scale, ButtonStyle::DESTRUCTIVE);
    button_styles(theme, "CrimsonRow", scale, ButtonStyle::ROW);
    button_styles(theme, "CrimsonIcon", scale, ButtonStyle::SECONDARY, 14);
    button_styles(theme, "CrimsonQuietIcon", scale, ButtonStyle::QUIET, 14);
    button_styles(theme, "CrimsonDangerIcon", scale, ButtonStyle::DESTRUCTIVE, 14);
    const Color none(0, 0, 0, 0);
    const auto transparent = box(none, none);
    for (const char *type : {"BaseButton", "MenuButton", "LinkButton", "ColorPickerButton", "AcceptDialog", "ConfirmationDialog"}) {
        theme->set_stylebox("focus", type, transparent);
    }
    for (const char *type : {"sample_focus", "picker_focus_rectangle", "picker_focus_circle"}) {
        theme->set_stylebox(type, "ColorPicker", transparent);
    }
    theme->set_stylebox("preset_focus", "ColorPresetButton", transparent);
    button_styles(theme, "OptionButton", scale, ButtonStyle::SECONDARY);
    button_styles(theme, "CrimsonSelect", scale, ButtonStyle::SECONDARY);
    const auto select = rounded_box(Color::html("#2a2b2c"), none, 8, 3, scale);
    const auto select_hover = rounded_box(Color::html("#303132"), none, 8, 3, scale);
    const auto select_open = rounded_box(Color::html("#343537"), Color::html("#414245"), 8, 3, scale);
    for (const char *type : {"OptionButton", "CrimsonSelect"}) {
        theme->set_stylebox("normal", type, select);
        theme->set_stylebox("hover", type, select_hover);
        theme->set_stylebox("pressed", type, select_open);
        theme->set_stylebox("hover_pressed", type, select_open);
    }
    theme->set_color("font_color", "CrimsonSelect", Color::html("#e5e6e8"));
    theme->set_color("font_focus_color", "CrimsonSelect", Color::html("#e5e6e8"));
    container_styles(theme, scale, composite_popups);
    const int menu_label_inset = theme->get_constant("item_start_padding", "PopupMenu") +
            theme->get_constant("h_separation", "PopupMenu");
    const auto option = rounded_box(none, none, spacing::lg, 0, scale);
    const auto candidate = rounded_box(Color::html("#3c3d40"), none, spacing::lg, 0, scale);
    const auto menu_option = rounded_box(none, none, 0, 0, scale);
    const auto menu_candidate = rounded_box(popup_row_highlight(), none, 0, 0, scale);
    const auto hover_row = rounded_box(popup_row_highlight(), none, spacing::md, spacing::xs, scale);
    menu_option->set_content_margin(SIDE_LEFT, menu_label_inset);
    menu_candidate->set_content_margin(SIDE_LEFT, menu_label_inset);
    for (const auto &style : {option, candidate, menu_option, menu_candidate}) {
        style->set_content_margin(SIDE_RIGHT, 35 * scale);
    }
    for (const char *state : {"normal", "pressed"}) {
        theme->set_stylebox(state, "CrimsonCompactSelectOption", option);
        theme->set_stylebox(state, "CrimsonCompactSelectCandidate", candidate);
        theme->set_stylebox(state, "CrimsonMenuSelectOption", menu_option);
        theme->set_stylebox(state, "CrimsonMenuSelectCandidate", menu_candidate);
    }
    for (const char *state : {"hover", "hover_pressed"}) {
        theme->set_stylebox(state, "CrimsonCompactSelectOption", candidate);
        theme->set_stylebox(state, "CrimsonMenuSelectOption", menu_candidate);
        theme->set_stylebox(state, "CrimsonPickerOption", hover_row);
    }
    for (const char *state : {"font_color", "font_hover_color", "font_focus_color", "font_pressed_color", "font_hover_pressed_color"}) {
        theme->set_color(state, "CrimsonCompactSelectOption", Color::html("#e5e6e8"));
    }
    for (const char *state : {"font_color", "font_hover_color", "font_focus_color", "font_pressed_color", "font_hover_pressed_color"}) {
        theme->set_color(state, "CrimsonCompactSelectCandidate", Color::html("#ffffff"));
        theme->set_color(state, "CrimsonMenuSelectCandidate", Color::html("#ffffff"));
    }
    theme->set_color("font_disabled_color", "CrimsonCompactSelectOption", color(Surface::DISABLED));
    field_styles(theme, scale);
    check_styles(theme, scale);
    range_styles(theme, scale);
    theme->set_font_size("font_size", "CrimsonCompactSelectOption", std::lround(12 * scale));
    theme->set_font_size("font_size", "CrimsonMenuSelectOption", theme->get_font_size("font_size", "PopupMenu"));
    list_styles(theme, scale);
    dialog_styles(theme, scale);
    icons(theme, scale);
    return theme;
}

int pixels(Node *host, double logical) {
    for (Node *node = host; node; node = node->get_parent()) {
        if (auto *control = Object::cast_to<Control>(node)) {
            const double scale = control->get_theme_default_base_scale();
            if (scale > 0.0) {
                return std::lround(logical * scale);
            }
        } else if (auto *window = Object::cast_to<Window>(node)) {
            const double scale = window->get_theme_default_base_scale();
            return std::lround(logical * (scale > 0.0 ? scale : 1.0));
        }
    }
    return std::lround(logical);
}

Ref<Texture2D> icon(Node *owner, const StringName &name, bool highlighted) {
    if (auto *control = Object::cast_to<Control>(owner)) {
        if (highlighted && control->has_theme_icon(name, "CrimsonHighlightedIcons")) {
            return control->get_theme_icon(name, "CrimsonHighlightedIcons");
        }
        return control->get_theme_icon(name, "CrimsonIcons");
    }
    if (auto *window = Object::cast_to<Window>(owner)) {
        if (highlighted && window->has_theme_icon(name, "CrimsonHighlightedIcons")) {
            return window->get_theme_icon(name, "CrimsonHighlightedIcons");
        }
        return window->get_theme_icon(name, "CrimsonIcons");
    }
    return {};
}

} // namespace crimson::editor_ui

#endif
