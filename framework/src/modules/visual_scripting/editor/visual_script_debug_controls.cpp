// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_window.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/visual_scripting/editor/visual_script_editor_style.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace crimson::visual_scripting {
namespace {

using namespace godot;
namespace ui = crimson::editor_ui;

/// Give each command a visible label, a native icon, and a discoverable shortcut.
struct DebugAction {
    /// Stable action understood by the editor debugger bridge.
    const char *action;
    /// Node name used by editor verification.
    const char *name;
    /// Button label.
    const char *label;
    /// Godot editor icon name.
    const char *icon;
    /// Tooltip explaining the action. The shortcut is shown from the key and modifier below.
    const char *help;
    /// The command's graph-local key.
    Key key;
    /// Whether the keyboard shortcut includes Shift.
    bool shift;
};

/// The most common actions come first in visual and keyboard focus order.
constexpr DebugAction actions[] = {
    {"continue", "VisualScriptDebugContinue", "Continue", "DebugContinue",
            "Resume execution until the next breakpoint.", KEY_F5, false},
    {"step_over", "VisualScriptDebugStepOver", "Step Over", "DebugNext",
            "Run this node and pause at the next node in this graph or its caller.", KEY_F10, false},
    {"step_into", "VisualScriptDebugStepIn", "Step In", "DebugStep",
            "Pause at the next node, including inside a function or macro called from this node.", KEY_F11, false},
    {"step_out", "VisualScriptDebugStepOut", "Step Out", "DebugOut",
            "Finish the current graph call and pause in its caller when there is one.", KEY_F11, true},
};

/// Attach a stock editor control to its owning container.
template <typename T>
T *add(Node *parent, const StringName &name = {}) {
    auto *child = memnew(T);
    if (!name.is_empty()) {
        child->set_name(name);
    }
    parent->add_child(child);
    return child;
}

/// Use a single quiet bottom keyline where one debugger region meets another.
void panel_style(PanelContainer *panel, GraphTone surface, int horizontal, int vertical) {
    const Color none(0, 0, 0, 0);
    const auto style = ui::box(graph_color(surface), none, horizontal, vertical,
            panel->get_theme_default_base_scale());
    style->set_border_width(SIDE_BOTTOM, ui::pixels(panel, 1));
    style->set_border_color(graph_color(GraphTone::DIVIDER));
    panel->add_theme_stylebox_override("panel", style);
}

/// Keep debugger groups on the same surface as their values.
TreeItem *add_section(Tree *tree, TreeItem *root, const String &section, const Array &rows) {
    if (rows.is_empty()) {
        return nullptr;
    }
    auto *group = tree->create_item(root);
    group->set_text(0, section);
    group->set_custom_color(0, graph_color(GraphTone::TEXT));
    group->set_custom_font(0, tree->get_theme_font("font", "CrimsonHeading"));
    group->set_custom_minimum_height(ui::pixels(tree, 24));
    for (int32_t column = 0; column < 2; ++column) {
        group->set_selectable(column, false);
    }
    group->set_expand_right(0, true);
    return group;
}

/// Retain multi-line inspection context while keeping the cell value the primary text.
String describe_value(const Dictionary &value) {
    const String preview = value.get("value", String());
    const String type = value.get("type", String());
    const String source = value.get("source", String());
    const String detail = value.get("detail", String());
    const String state = value.get("state", String());
    String help = preview;
    if (!type.is_empty()) {
        help += String("  (") + type + ")";
    }
    if (!source.is_empty()) {
        help += String("\n") + source;
    }
    if (state == "last") {
        help += String("\nLast value in this invocation");
    }
    if (!detail.is_empty() && detail != preview) {
        help += String("\n") + detail;
    }
    return help;
}

/// Keep labels, concise values, and actionable provenance visible at typical sidebar widths.
TreeItem *add_value(Tree *tree, TreeItem *section, const Dictionary &value, const String &label,
        const String &subtitle = {}) {
    auto *item = tree->create_item(section);
    const String preview = value.get("value", String());
    const String state = value.get("state", "current");
    item->set_text(0, label);
    item->set_text(1, preview.replace("\n", " "));
    for (int32_t column = 0; column < 2; ++column) {
        item->set_text_overrun_behavior(column, TextServer::OVERRUN_TRIM_ELLIPSIS);
        item->set_auto_translate_mode(column, Node::AUTO_TRANSLATE_MODE_DISABLED);
    }
    item->set_custom_minimum_height(ui::pixels(tree, 24));
    item->set_custom_color(0, graph_color(GraphTone::MUTED));
    if (const Ref<Font> digits = tree->get_theme_font("font", "CrimsonDebugValue"); digits.is_valid()) {
        item->set_custom_font(1, digits);
    }
    if (state != "current") {
        item->set_custom_color(1, graph_color(GraphTone::MUTED));
    }
    if (state == "unavailable") {
        item->set_custom_font_size(1, ui::pixels(tree, 11));
    }
    item->set_tooltip_text(0, subtitle.is_empty() ? label : label + String("\n") + subtitle);
    item->set_tooltip_text(1, describe_value(value));
    return item;
}

/// Resolve the display name for a watched or temporarily unavailable data pin.
String watch_pin_label(const Dictionary &address) {
    const String name = address.get("name", String());
    const String title = address.get("title", String());
    return title.is_empty() || title == name ? name : title + String(U" \u00B7 ") + name;
}

/// Lead a narrow watch row with the pin name, while retaining its node context.
String watch_row_label(const Dictionary &address) {
    const String name = address.get("name", String());
    const String title = address.get("title", String());
    return title.is_empty() || title == name ? name : name + String(U" \u00B7 ") + title;
}

} // namespace

Dictionary VisualScriptWindow::debug_pin_address(const PendingPin &pin) const {
    if (script.is_null() || !script->get_path().begins_with("res://") || pin.node <= 0 || pin.pin.is_empty() ||
            pin.descriptor.is_empty() || bool(pin.descriptor.get("exec", false))) {
        return {};
    }
    Dictionary address;
    address["script"] = script->get_path();
    address["graph"] = active_graph;
    address["node"] = pin.node;
    address["output"] = pin.output;
    address["pin"] = pin.pin;
    address["name"] = pin.descriptor.get("label", String(pin.pin));
    const auto widget = widgets.find(pin.node);
    if (widget != widgets.end()) {
        address["title"] = widget->second->get_title();
    }
    const StringName class_name = pin.descriptor.get("class", StringName());
    const auto value_type = static_cast<Variant::Type>(int64_t(pin.descriptor.get("type", Variant::NIL)));
    address["type"] = class_name.is_empty() ? Variant::get_type_name(value_type) : String(class_name);
    return address;
}

Dictionary VisualScriptWindow::debug_pin_value(const Dictionary &address) const {
    const String script_path = address.get("script", String());
    const auto graph = int64_t(address.get("graph", -1));
    if (!debug_snapshot.is_empty() && script_path == String(debug_snapshot.get("script", String()))) {
        const auto key = std::make_tuple(graph, int64_t(address.get("node", 0)), bool(address.get("output", false)),
                StringName(address.get("pin", StringName())));
        const auto found = debug_pin_samples.find(key);
        if (found != debug_pin_samples.end()) {
            return found->second;
        }
    }
    Dictionary unavailable = address.duplicate();
    unavailable["state"] = "unavailable";
    if (debug_snapshot.is_empty()) {
        unavailable["value"] = "Waiting for pause";
    } else if (String(debug_snapshot.get("script", String())) != script_path ||
            !Array(debug_snapshot.get("active_graphs", Array())).has(graph)) {
        unavailable["value"] = "Outside this call stack";
    } else if (Array(debug_snapshot.get("limited_graphs", Array())).has(graph)) {
        unavailable["value"] = "Value not captured";
    } else {
        unavailable["value"] = "Not evaluated";
    }
    return unavailable;
}

String VisualScriptWindow::debug_pin_tooltip(const PendingPin &pin) const {
    if (!debug_paused || debug_snapshot.is_empty()) {
        return {};
    }
    const Dictionary address = debug_pin_address(pin);
    if (address.is_empty()) {
        return {};
    }
    const Dictionary value = debug_pin_value(address);
    return watch_pin_label(value) + String("\n") + describe_value(value);
}

void VisualScriptWindow::debug_watch_clicked(TreeItem *item, int64_t column, int64_t button_id, int64_t mouse_button) {
    if (!item || column != 1 || button_id != 0 || mouse_button != MOUSE_BUTTON_LEFT ||
            item->get_metadata(0).get_type() != Variant::DICTIONARY) {
        return;
    }
    debug_watch_set(item->get_metadata(0), false);
}

void VisualScriptWindow::build_debug_controls(Node *workspace, Control *right_sidebar) {
    auto *strip = add<PanelContainer>(workspace, "VisualScriptDebugToolbar");
    panel_style(strip, GraphTone::SIDEBAR, ui::spacing::lg, ui::spacing::xs);
    debug_toolbar = strip;
    auto *toolbar = add<HBoxContainer>(strip);
    toolbar->add_theme_constant_override("separation", ui::pixels(toolbar, 4));

    debug_status = ui::label(toolbar, "Running", ui::LabelStyle::HEADING);
    debug_status->set_name("VisualScriptDebugStatus");
    debug_status->set_custom_minimum_size(Vector2(ui::pixels(toolbar, 64), 0));
    debug_status->add_theme_font_size_override("font_size", ui::pixels(toolbar, 13));
    const Ref<Theme> editor_theme = EditorInterface::get_singleton()->get_editor_theme();
    for (const DebugAction &action : actions) {
        const Key shortcut = action.shift ? static_cast<Key>(action.key | KEY_MASK_SHIFT) : action.key;
        auto *button = ui::button(toolbar, action.label, action.help,
                callable_mp(this, &VisualScriptWindow::debug_command).bind(StringName(action.action)),
                ui::ButtonStyle::SECONDARY, shortcut);
        button->set_name(action.name);
        button->set_theme_type_variation("GraphToolbarButton");
        button->set_custom_minimum_size(Vector2(0, ui::pixels(button, 28)));
        if (editor_theme.is_valid() && editor_theme->has_icon(action.icon, "EditorIcons")) {
            button->set_button_icon(editor_theme->get_icon(action.icon, "EditorIcons"));
        }
        debug_actions.push_back(button);
    }

    auto *pane = add<PanelContainer>(right_sidebar, "VisualScriptDebugPane");
    pane->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    pane->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    pane->set_stretch_ratio(0.45);
    pane->set_custom_minimum_size(Vector2(0, ui::pixels(pane, 124)));
    const auto pane_style = ui::box(graph_color(GraphTone::PANE), graph_color(GraphTone::DIVIDER), 0, 0,
            pane->get_theme_default_base_scale());
    pane_style->set_corner_radius_all(ui::pixels(pane, 12));
    pane_style->set_anti_aliased(true);
    pane->add_theme_stylebox_override("panel", pane_style);
    debug_pane = pane;
    auto *content = add<VBoxContainer>(pane);
    content->add_theme_constant_override("separation", 0);
    auto *heading = add<PanelContainer>(content);
    const Color clear(0, 0, 0, 0);
    heading->add_theme_stylebox_override("panel", ui::box(clear, clear, 12, 5, heading->get_theme_default_base_scale()));
    heading->set_custom_minimum_size(Vector2(0, ui::pixels(heading, 32)));
    auto *title = ui::label(heading, "Debugger", ui::LabelStyle::HEADING);
    title->add_theme_font_size_override("font_size", ui::pixels(title, 13));

    auto *context = add<PanelContainer>(content);
    panel_style(context, GraphTone::PANE, ui::spacing::lg, ui::spacing::md);
    auto *context_lines = add<VBoxContainer>(context);
    context_lines->add_theme_constant_override("separation", ui::pixels(context, 3));
    debug_context = ui::label(context_lines, "", ui::LabelStyle::HEADING);
    debug_context->set_name("VisualScriptDebugContext");
    debug_context->add_theme_font_size_override("font_size", ui::pixels(debug_context, 14));
    debug_context->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    debug_context->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    debug_context->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    debug_location = ui::label(context_lines, "", ui::LabelStyle::MUTED);
    debug_location->set_name("VisualScriptDebugLocation");
    debug_location->add_theme_font_size_override("font_size", ui::pixels(debug_location, 12));
    debug_location->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    debug_location->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    debug_location->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);

    debug_values = add<Tree>(content, "VisualScriptDebugValues");
    debug_values->set_accessibility_name("Paused graph values");
    debug_values->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    debug_values->set_columns(2);
    debug_values->set_column_titles_visible(false);
    debug_values->set_column_expand_ratio(0, 5);
    debug_values->set_column_expand_ratio(1, 7);
    debug_values->set_column_custom_minimum_width(0, ui::pixels(debug_values, 84));
    debug_values->set_column_custom_minimum_width(1, ui::pixels(debug_values, 54));
    for (int32_t column = 0; column < 2; ++column) {
        debug_values->set_column_clip_content(column, true);
    }
    debug_values->set_hide_root(true);
    debug_values->set_h_scroll_enabled(false);
    debug_values->add_theme_font_size_override("font_size", ui::pixels(debug_values, 12));
    debug_values->add_theme_constant_override("v_separation", ui::pixels(debug_values, 2));
    debug_values->add_theme_constant_override("item_margin", ui::pixels(debug_values, 12));
    debug_values->add_theme_constant_override("draw_guides", 0);
    debug_values->add_theme_constant_override("draw_relationship_lines", 0);
    debug_values->add_theme_icon_override("arrow", ui::icon(debug_values, "chevron_down"));
    debug_values->add_theme_icon_override("arrow_collapsed", ui::icon(debug_values, "chevron_right"));
    const auto theme = get_theme();
    Ref<FontVariation> digits;
    digits.instantiate();
    digits->set_base_font(debug_values->get_theme_default_font());
    Dictionary features;
    features["tnum"] = 1;
    digits->set_opentype_features(features);
    theme->set_font("font", "CrimsonDebugValue", digits);
    debug_values->add_theme_stylebox_override("panel", ui::box(clear, clear, 9, 3, debug_values->get_theme_default_base_scale()));
    debug_values->set_select_mode(Tree::SELECT_ROW);
    debug_values->connect("button_clicked", callable_mp(this, &VisualScriptWindow::debug_watch_clicked));
    debug_set_run_state(false, false, false);
}

void VisualScriptWindow::debug_set_run_state(bool running, bool paused, bool commands_ready) {
    debug_paused = running && paused;
    debug_commands_ready = debug_paused && commands_ready;
    debug_toolbar->set_visible(running);
    debug_status->set_text(debug_paused ? "Paused" : "Running");
    debug_status->add_theme_color_override("font_color", debug_paused ? Color::html("#ffc76a") : graph_color(GraphTone::MUTED));
    for (auto *button : debug_actions) {
        button->set_disabled(!debug_commands_ready);
        button->set_focus_mode(debug_commands_ready ? Control::FOCUS_ALL : Control::FOCUS_NONE);
        button->set_default_cursor_shape(debug_commands_ready ? Control::CURSOR_POINTING_HAND : Control::CURSOR_ARROW);
    }
    if (!debug_paused) {
        debug_apply_snapshot(Dictionary());
    }
}

void VisualScriptWindow::debug_apply_snapshot(const Dictionary &snapshot) {
    debug_snapshot = debug_paused ? snapshot : Dictionary();
    debug_pin_samples.clear();
    for (const Dictionary &row : Array(debug_snapshot.get("pins", Array()))) {
        const auto key = std::make_tuple(int64_t(row.get("graph", -1)), int64_t(row.get("node", 0)),
                bool(row.get("output", false)), StringName(row.get("pin", StringName())));
        debug_pin_samples.try_emplace(key, row);
    }
    debug_refresh_pin_labels();
    if (auto *old_root = debug_values->get_root()) {
        for (auto *section = old_root->get_first_child(); section; section = section->get_next()) {
            debug_section_collapsed[section->get_text(0)] = section->is_collapsed();
        }
    }
    debug_values->clear();
    const String path = debug_snapshot.get("script", script.is_valid() ? script->get_path() : String());
    const Array watches = debug_watch_records(path);
    debug_pane->set_visible(debug_toolbar->is_visible() || !watches.is_empty());
    const String instance = debug_snapshot.get("instance", String());
    const String actor = debug_snapshot.get("instance_label", instance.get_file());
    const String graph_name = debug_snapshot.get("title", "Graph");
    String paused_node = debug_snapshot.get("node_title", String());
    if (paused_node.is_empty()) {
        const auto widget = widgets.find(int64_t(debug_snapshot.get("node", 0)));
        if (widget != widgets.end()) {
            paused_node = widget->second->get_title();
        }
    }
    debug_context->set_text(debug_snapshot.is_empty() ? String("Waiting for pause") :
            paused_node.is_empty() ? String("Paused") : String("Paused before ") + paused_node);
    debug_location->set_text(debug_snapshot.is_empty() ? String() :
            actor.is_empty() ? graph_name : graph_name + String(U" \u00B7 ") + actor);
    debug_location->set_tooltip_text(instance.is_empty() ? String() : graph_name + String("\n") + instance);
    debug_location->set_visible(!debug_snapshot.is_empty());

    TreeItem *root = debug_values->create_item();
    if (auto *section = add_section(debug_values, root, "Watches", watches)) {
        const Ref<Texture2D> remove = ui::icon(debug_values, "close");
        for (const Dictionary &watch : watches) {
            const Dictionary value = debug_pin_value(watch);
            String origin = value.get("source", String());
            if (String(value.get("state", String())) == "last") {
                origin = origin.is_empty() ? String("Last value") : String(U"Last value \u00B7 ") + origin;
            }
            auto *item = add_value(debug_values, section, value, watch_row_label(value), origin);
            item->set_metadata(0, watch);
            if (remove.is_valid()) {
                item->add_button(1, remove, 0, false, "Remove Watch", "Remove the watched pin.");
            }
        }
        const auto previous = debug_section_collapsed.find(String("Watches"));
        section->set_collapsed(previous != debug_section_collapsed.end() && previous->second);
    }
    const auto append = [&](const char *title, const char *key, bool provenance, bool collapsed = false) {
        const Array values = debug_snapshot.get(key, Array());
        auto *section = add_section(debug_values, root, title, values);
        if (!section) {
            return;
        }
        for (const Dictionary &value : values) {
            const String name = value.get("name", String());
            const bool computed = String(key) == "computed";
            const String origin = computed ? String(value.get("title", String())) :
                    provenance ? String(value.get("source", String())) : String();
            add_value(debug_values, section, value, name, origin == name ? String() : origin);
        }
        const auto previous = debug_section_collapsed.find(String(title));
        section->set_collapsed(previous != debug_section_collapsed.end() ? previous->second : collapsed);
    };
    append("Inputs", "inputs", true);
    append("Computed", "computed", false, true);
    append("Local Variables", "locals", false, true);
    append("Instance Variables", "members", false, true);
    append("Call Stack", "stack", true, true);
    debug_values->set_visible(root->get_child_count() != 0);
}

bool VisualScriptWindow::debug_shortcut(const Ref<InputEventKey> &event) {
    if (!debug_commands_ready || event->is_command_or_control_pressed() || event->is_alt_pressed() ||
            !debug_toolbar->is_visible_in_tree()) {
        return false;
    }
    for (const DebugAction &action : actions) {
        if (event->get_keycode() == action.key && event->is_shift_pressed() == action.shift) {
            debug_command(action.action);
            return true;
        }
    }
    return false;
}

} // namespace crimson::visual_scripting

#endif
