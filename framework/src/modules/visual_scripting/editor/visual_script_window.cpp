// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_window.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_graph.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/visual_scripting/editor/visual_script_authored_enums.hpp"
#include "modules/visual_scripting/editor/visual_script_asset_field.hpp"
#include "modules/visual_scripting/editor/visual_script_custom_code.hpp"
#include "modules/visual_scripting/editor/visual_script_drop_choice.hpp"
#include "modules/visual_scripting/editor/visual_script_editor_style.hpp"
#include "modules/visual_scripting/editor/visual_script_help.hpp"
#include "modules/visual_scripting/editor/visual_script_organization.hpp"
#include "modules/visual_scripting/editor/visual_script_wire_tools.hpp"
#include "modules/visual_scripting/editor/visual_script_panels.hpp"
#include "modules/visual_scripting/editor/visual_script_tag_field.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_builtin_data.hpp"
#include "modules/visual_scripting/visual_script_cast_input.hpp"
#include "modules/visual_scripting/visual_script_collections.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"
#include "modules/visual_scripting/visual_script_value_nodes.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/graph_edit.hpp>
#include <godot_cpp/classes/graph_element.hpp>
#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/h_split_container.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/menu_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/popup_panel.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/undo_redo.hpp>
#include <godot_cpp/classes/v_split_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace crimson::visual_scripting {
namespace {

using namespace godot;
namespace ui = crimson::editor_ui;

/// Port indices in the editor are projections of the runtime's named pins.
enum PortType { EXECUTION, DATA };

/// Native canvas metadata identifying a node independently of its display title.
constexpr const char *node_id_key = "visual_script_node_id";
/// Preserve the middle of a routing dot as a drag handle instead of a socket hit zone.
constexpr const char *routing_dot_key = "visual_script_routing_dot";
/// Stable keys used by the persisted graph model.
constexpr char ready_kind[] = "ready";
constexpr char print_kind[] = "print";
constexpr char create_kind[] = "create_node";
constexpr char add_child_kind[] = "add_child";
constexpr char get_property_kind[] = "get_property";
constexpr char set_property_kind[] = "set_property";
constexpr char call_method_kind[] = "call_method";
constexpr char custom_script_kind[] = "custom_script";
/// Coalesce nearby edits, including successive letters in a graph field.
constexpr double autosave_delay = 0.35;
/// Identify port labels separately from other interactive children of a graph node.
constexpr char pin_label_direction[] = "crimson_pin_output";
/// Mark execution captions for paint and hover without making them wire or context targets.
constexpr char flow_hover_label_direction[] = "crimson_flow_hover_output";
/// Preserve the type and authored hint for an unpaused pin label.
constexpr char pin_label_hint[] = "crimson_pin_hint";
constexpr char input_socket_hint[] = "crimson_input_hint";
constexpr char output_socket_hint[] = "crimson_output_hint";
/// Keep the live type presentation separate from the native slot's wiring contract.
constexpr char input_pin_presentation[] = "crimson_input_presentation";
constexpr char output_pin_presentation[] = "crimson_output_presentation";
/// Preserve the author-facing caption and optional help through a type-only refresh.
constexpr char input_pin_caption[] = "crimson_input_caption";
constexpr char output_pin_caption[] = "crimson_output_caption";
constexpr char input_pin_help[] = "crimson_input_help";
constexpr char reference_pin_caption[] = "crimson_reference_caption";
/// Match the compiler's maximum number of ordered Sequence outputs.
constexpr int64_t sequence_output_limit = 64;

/// Construct an editor-owned record from named fields.
template <typename... Fields>
Dictionary record_from(Fields &&...fields) {
    const Array values = Array::make(std::forward<Fields>(fields)...);
    Dictionary record;
    for (int64_t index = 0; index < values.size(); index += 2) {
        record[values[index]] = values[index + 1];
    }
    return record;
}

/// Preserve an action ID and its payload when a native menu is rebuilt for another target.
void menu_action(PopupMenu *menu, const String &label, const Dictionary &command, Key accelerator = KEY_NONE,
        const StringName &glyph = {}) {
    const int32_t index = menu->get_item_count();
    menu->add_item(label, index, accelerator);
    menu->set_item_metadata(index, command);
    set_graph_menu_icon(menu, index, glyph);
}

/// Identify execution independently of the pin's authored label or ID.
bool is_execution(const Dictionary &pin) {
    return static_cast<bool>(pin.get("exec", false));
}

/// The editor's pin indices are independent of each pin's type and saved ID.
Dictionary make_pin(const StringName &id, const String &label, Variant::Type type, const StringName &object_class = {}) {
    Dictionary descriptor;
    descriptor["id"] = id;
    descriptor["label"] = label;
    descriptor["type"] = static_cast<int64_t>(type);
    if (!object_class.is_empty()) {
        descriptor["class"] = object_class;
    }
    return descriptor;
}

/// Read the graph's class and nodes without sharing mutable model collections.
Array graph_array(const Dictionary &graph, const char *key) {
    const Variant value = graph.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Allocate a Godot child owned by the given container.
template <typename T>
T *add(Node *parent) {
    auto *child = memnew(T);
    parent->add_child(child);
    return child;
}

/// Theme the stock canvas and native tree to match Crimson's tools.
Ref<Theme> graph_theme(double scale, const Window *owner) {
    auto theme = ui::create_theme(owner, scale);
    const Color none(0, 0, 0, 0);
    ui::style_graph_canvas(theme, scale);
    theme->set_type_variation("GraphCompactContextMenu", "PopupMenu");
    theme->set_font_size("font_size", "GraphCompactContextMenu", std::lround(ui::graph_pin_font_size * scale));
    theme->set_font_size("font_separator_size", "GraphCompactContextMenu", std::lround(10 * scale));
    theme->set_constant("v_separation", "GraphCompactContextMenu", std::lround(ui::spacing::sm * scale));
    for (const char *type : {"GraphSaveButton", "GraphToolbarButton"}) {
        const StringName base_type = String(type) == "GraphSaveButton" ? StringName("CrimsonPrimary") :
                String(type) == "GraphToolbarButton" ? StringName("CrimsonQuiet") : StringName("Button");
        theme->set_type_variation(type, base_type);
        theme->set_constant("outline_size", type, 0);
    }
    for (const char *type : {"HSplitContainer", "VSplitContainer", "SplitContainer"}) {
        theme->set_constant("separation", type, std::lround(6 * scale));
        theme->set_constant("minimum_grab_thickness", type, std::lround(6 * scale));
        theme->set_stylebox("split_bar_background", type, ui::box(none, none, 0, 0, scale));
    }
    theme->set_type_variation("GraphPickerSearch", "CrimsonPopupSearch");
    theme->set_stylebox("normal", "GraphPickerSearch", ui::rounded_box(
            ui::color(ui::Surface::HEADER), ui::color(ui::Surface::OUTLINE), 8, 3, scale, 6));
    theme->set_type_variation("GraphPickerResults", "Tree");
    theme->set_stylebox("panel", "GraphPickerResults", ui::box(none, none, 0, 0, scale));
    const auto selected = ui::rounded_box(Color::html("#383951"), Color::html("#575a8c"), 0, 0, scale, 4);
    const auto hovered = ui::rounded_box(Color::html("#2b2c30"), none, 0, 0, scale, 4);
    for (const char *state : {"selected", "selected_focus", "hovered_selected", "hovered_selected_focus"}) {
        theme->set_stylebox(state, "GraphPickerResults", selected);
    }
    theme->set_stylebox("hovered", "GraphPickerResults", hovered);
    theme->set_color("font_selected_color", "GraphPickerResults", Color::html("#fbfaff"));
    theme->set_font_size("font_size", "GraphPickerResults", std::lround(12 * scale));
    theme->set_constant("v_separation", "GraphPickerResults", std::max(1L, std::lround(scale)));
    theme->set_constant("inner_item_margin_top", "GraphPickerResults", std::lround(scale));
    theme->set_constant("inner_item_margin_bottom", "GraphPickerResults", std::lround(scale));
    theme->set_constant("inner_item_margin_left", "GraphPickerResults", std::lround(ui::spacing::xs * scale));
    theme->set_constant("inner_item_margin_right", "GraphPickerResults", std::lround(ui::spacing::xs * scale));
    theme->set_constant("item_margin", "GraphPickerResults", std::lround(15 * scale));
    theme->set_constant("icon_h_separation", "GraphPickerResults", std::lround(ui::spacing::xs * scale));
    theme->set_icon("arrow", "GraphPickerResults", theme->get_icon("chevron_down", "CrimsonIcons"));
    theme->set_icon("arrow_collapsed", "GraphPickerResults", theme->get_icon("chevron_right", "CrimsonIcons"));
    theme->set_icon("arrow_collapsed_mirrored", "GraphPickerResults", theme->get_icon("chevron_left", "CrimsonIcons"));
    theme->set_type_variation("GraphPickerScrollBar", "VScrollBar");
    const auto track = ui::rounded_box(ui::color(ui::Surface::HEADER), ui::color(ui::Surface::RULE), 5, 0, scale, 4);
    const auto thumb = ui::rounded_box(Color::html("#555760"), none, 4, 3, scale, 3);
    const auto thumb_active = ui::rounded_box(Color::html("#777a89"), none, 4, 3, scale, 3);
    theme->set_stylebox("scroll", "GraphPickerScrollBar", track);
    theme->set_stylebox("scroll_focus", "GraphPickerScrollBar", track);
    theme->set_stylebox("grabber", "GraphPickerScrollBar", thumb);
    theme->set_stylebox("grabber_highlight", "GraphPickerScrollBar", thumb_active);
    theme->set_stylebox("grabber_pressed", "GraphPickerScrollBar", thumb_active);
    const Ref<Theme> editor_theme = EditorInterface::get_singleton()->get_editor_theme();
    if (editor_theme.is_valid()) {
        for (const char *name : {"MemberMethod", "Signal", "Edit", "MemberProperty", "MemberConstant", "Node", "Add", "AudioStream",
                     "Resource", "GuiOptionArrow", "Control", "Callable", "Script", "VisualShader", "ActionCut",
                     "ActionCopy", "ActionPaste", "Duplicate", "Remove", "Search", "ZoomReset", "Collapse",
                     "Unlinked", "GuiVisibilityVisible", "GuiVisibilityHidden", "ExternalLink", "Breakpoint", "Reload",
                     "VisualShaderNodeComment", "ToolMove", "MoveLeft", "MoveRight"}) {
            if (editor_theme->has_icon(name, "EditorIcons")) {
                theme->set_icon(name, "EditorIcons", editor_theme->get_icon(name, "EditorIcons"));
            }
        }
    }
    return theme;
}

/// Reserve role color for the icon and narrow marker on each node.
Color node_accent(const StringName &kind) {
    const String name = kind;
    if (name == "ready" || name == "lifecycle" || name == "native_signal" || name == "custom_event" || name == "emit_event") return Color::html("#ed9488");
    if (name == "branch" || name == "switch" || name == "switch_enum" || name == "sequence" || name == "while" || name == "for" || name == "foreach" ||
            name == "delay" || name == "await_signal" ||
            name == "break" || name == "continue" || name == "do_once" || name == "do_n" || name == "flip_flop" ||
            name == "gate") return Color::html("#79c8ca");
    if (name == "get_property" || name == "get_variable" || name == "self" || name == "scene_node" || name == "operator" ||
            name == "literal" || name == "select") return Color::html("#8fca9d");
    if (name == "call_macro" || name == "macro_entry" || name == "function_entry" || name == "return") return Color::html("#c2a3e6");
    if (name == "set_property" || name == "set_variable") return Color::html("#edb362");
    if (name == "create_node" || name == "add_child") return Color::html("#98b9e2");
    return Color::html("#a6b1c8");
}

/// Give each role a readable glyph as well as a color.
StringName node_role_icon(const StringName &kind) {
    const String name = kind;
    if (name == custom_script_kind) return {};
    if (name == "ready" || name == "lifecycle" || name == "native_signal" || name == "custom_event" || name == "emit_event" || name == "await_signal") return "Signal";
    if (name == "call_method" || name == "call_function" || name == "call_macro" || name == "macro_entry" || name == "function_entry" || name == "print") return "MemberMethod";
    if (name == "set_property" || name == "set_variable") return "Edit";
    if (name == "get_property" || name == "get_variable") return "MemberProperty";
    if (name == "literal") return "MemberConstant";
    if (name == "self" || name == "scene_node") return "Node";
    if (name == "create_node" || name == "add_child") return "Add";
    return "Control";
}

/// Color the titlebar by role while preserving type gradients on value cards.
Color node_header(const StringName &kind, const Dictionary &node) {
    using ui::GraphNodeHeader;
    const String name = kind;
    if (name == "ready" || name == "lifecycle" || name == "native_signal" || name == "custom_event" || name == "emit_event") {
        return ui::graph_node_header(GraphNodeHeader::EVENT);
    }
    if (name == custom_script_kind) {
        return ui::graph_node_header(GraphNodeHeader::CUSTOM_CODE);
    }
    if (name == "call_method" || name == "call_function" || name == "call_macro" || name == "create_node" ||
            name == "add_child" || name == "print" || name == "macro_entry" || name == "function_entry" || name == "return") {
        const Dictionary member = node.get("member", Dictionary());
        return ui::graph_node_header(static_cast<bool>(member.get("const", false)) ? GraphNodeHeader::VALUE : GraphNodeHeader::CALL);
    }
    if (name == "get_property" || name == "literal" || name == "operator" || name == "scene_node" || name == "self" || name == "select") {
        return ui::graph_node_header(GraphNodeHeader::VALUE);
    }
    return ui::graph_node_header(GraphNodeHeader::CONTROL);
}

/// Match familiar Blueprint value colors while keeping execution visually distinct.
Color pin_color(const Dictionary &pin) {
    return is_execution(pin) ? ui::graph_execution_color() :
            ui::graph_pin_color(static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", Variant::NIL))),
                    !StringName(pin.get("enum_type", StringName())).is_empty());
}

/// Display colors and glyph for the resolved pin without changing its wiring contract.
struct PinAppearance {
    ui::GraphValueShape shape = ui::GraphValueShape::SCALAR; ///< Glyph of the effective value type.
    Color ink; ///< Scalar color, Array element color, or Dictionary key color.
    std::optional<Color> secondary; ///< Independent Dictionary value color.
};

/// Read the ordered color lanes of a collection pin, using Variant gray for an unknown lane.
PinAppearance pin_appearance(const Dictionary &pin) {
    const auto shape = ui::graph_value_shape(static_cast<Variant::Type>(int64_t(pin.get("type", Variant::NIL))),
            !StringName(pin.get("enum_type", StringName())).is_empty());
    if (shape == ui::GraphValueShape::DICTIONARY) {
        return {shape, pin_color(pin.get("key", Dictionary())), pin_color(pin.get("value", Dictionary()))};
    }
    if (int64_t(pin.get("type", Variant::NIL)) == Variant::ARRAY) {
        return {shape, pin_color(pin.get("element", Dictionary())), {}};
    }
    return {shape, pin_color(pin), {}};
}

/// Derive one stable native widget name from the persisted graph node ID.
StringName widget_name(int64_t id) {
    return StringName("GraphNode_" + String::num_int64(id));
}

/// Read the persisted ID of a graph canvas widget, if it is still present.
int64_t widget_id(GraphEdit *canvas, const StringName &name) {
    const auto *widget = Object::cast_to<GraphNode>(canvas->get_node_or_null(NodePath(String(name))));
    return widget ? static_cast<int64_t>(widget->get_meta(node_id_key, 0)) : 0;
}

/// Look up a live widget's stable pin ID by native port index.
StringName pin_id(GraphNode *widget, bool output, int64_t port) {
    if (!widget || port < 0 || port >= (output ? widget->get_output_port_count() : widget->get_input_port_count())) {
        return {};
    }
    const int32_t index = static_cast<int32_t>(port);
    const Variant data = output ? widget->get_slot_metadata_right(widget->get_output_port_slot(index))
                                : widget->get_slot_metadata_left(widget->get_input_port_slot(index));
    return data.get_type() == Variant::DICTIONARY ? StringName(Dictionary(data).get("id", StringName())) : StringName(data);
}

/// Read a typed data pin as reflected or declared by its graph node.
Dictionary pin_descriptor(GraphNode *widget, bool output, int32_t port) {
    if (!widget || port < 0 || port >= (output ? widget->get_output_port_count() : widget->get_input_port_count())) {
        return {};
    }
    const Variant data = output ? widget->get_slot_metadata_right(widget->get_output_port_slot(port))
                                : widget->get_slot_metadata_left(widget->get_input_port_slot(port));
    return data.get_type() == Variant::DICTIONARY ? Dictionary(data) : Dictionary();
}

/// Read the cached visual type at a native port, falling back to the authored descriptor.
Dictionary presented_pin_descriptor(GraphNode *widget, bool output, int32_t port) {
    const int32_t count = output ? widget->get_output_port_count() : widget->get_input_port_count();
    if (port < 0 || port >= count) return {};
    const int32_t slot = output ? widget->get_output_port_slot(port) : widget->get_input_port_slot(port);
    auto *row = widget->get_child(slot);
    const auto key = output ? output_pin_presentation : input_pin_presentation;
    return row && row->has_meta(key) ? Dictionary(row->get_meta(key)) : pin_descriptor(widget, output, port);
}

/// Require execution pins or compatible data types during and after a wire drag.
bool compatible_ports(GraphNode *source, int32_t from, GraphNode *target, int32_t to) {
    if (!source || !target || source == target || pin_id(source, true, from).is_empty() || pin_id(target, false, to).is_empty()) {
        return false;
    }
    const int32_t from_type = source->get_output_port_type(from);
    const int32_t to_type = target->get_input_port_type(to);
    return from_type == to_type && (from_type == EXECUTION ||
            pins_compatible(pin_descriptor(source, true, from), pin_descriptor(target, false, to)));
}

/// Keep GraphEdit's native wiring gestures while filtering them by the full data type.
class VisualScriptCanvas final : public GraphEdit {
    GDCLASS(VisualScriptCanvas, GraphEdit)

    /// Paint pointer feedback above the native node layer without capturing input.
    Control *interaction_overlay = nullptr;
    /// Take hover hits only during native middle-button panning.
    Control *pan_hover = nullptr;
    /// Last compatible socket recognized by the same test GraphEdit uses to snap.
    std::optional<Vector2> compatible_socket;
    /// Source widget and port of the editor's pointer gesture.
    GraphNode *dragged_socket_node = nullptr;
    /// The source port's index in its current graph widget.
    int32_t dragged_socket_port = -1;
    /// Whether the fixed pin is an output.
    bool dragged_socket_output = false;
    /// Current free end of the drag in canvas coordinates.
    std::optional<Vector2> dragged_pointer;
    /// Start and current point of a modified marquee in canvas coordinates.
    Vector2 selection_start;
    Vector2 selection_end;
    /// The fixed selection mode for the current marquee, if any.
    std::optional<bool> add_to_selection;
    /// Restore the original membership outside the selection rectangle.
    std::set<GraphElement *> original_selection;

    /// Match the visible canvas after a dock resize, independently of its graph scroll offset.
    void fit_pan_hover() {
        if (!pan_hover) return;
        const Rect2 bounds = get_global_rect();
        pan_hover->set_global_position(bounds.position);
        pan_hover->set_size(bounds.size);
    }

    /// Restore native card and socket hover without changing GraphEdit's mouse capture.
    void stop_pan_hover() {
        if (pan_hover) pan_hover->hide();
    }

    /// Resolve card or frame title and edge under the pointer in native stacking order.
    GraphElement *selection_target(Vector2 point) {
        for (int32_t index = get_child_count() - 1; index >= 0; --index) {
            auto *element = Object::cast_to<GraphElement>(get_child(index));
            if (!element || !element->is_visible()) continue;
            const Rect2 rect(element->get_position(), element->get_size() * get_zoom());
            if (!rect.has_point(point)) continue;
            if (element->is_class("GraphFrame")) {
                const auto *title = Object::cast_to<Control>(element->call("get_titlebar_hbox"));
                const real_t title_height = title ? (title->get_position().y + title->get_size().y) * get_zoom() : 0;
                if (point.y >= rect.position.y + title_height && rect.grow(-ui::pixels(this, 10)).has_point(point)) continue;
            }
            return element;
        }
        return nullptr;
    }

    /// Leave a native input field, asset picker, or other focusable control in charge of its own click.
    static bool interactive_at(Control *container, Vector2 local) {
        if (!container->is_visible()) return false;
        for (int32_t index = container->get_child_count() - 1; index >= 0; --index) {
            auto *child = Object::cast_to<Control>(container->get_child(index));
            if (child && interactive_at(child, child->get_transform().affine_inverse().xform(local))) return true;
        }
        return container->get_mouse_filter() != MOUSE_FILTER_IGNORE && container->get_focus_mode() != FOCUS_NONE &&
                Rect2(Vector2(), container->get_size()).has_point(local);
    }

    /// Apply Shift-add or Ctrl-remove to a card or start a marquee on empty canvas.
    bool start_selection(const Ref<InputEventMouseButton> &button) {
        if (button.is_null() || !button->is_pressed() || button->get_button_index() != MOUSE_BUTTON_LEFT ||
                button->is_alt_pressed() ||
                !(button->is_shift_pressed() || button->is_command_or_control_pressed())) {
            return false;
        }
        grab_focus();
        const bool add_selection = !button->is_command_or_control_pressed();
        if (auto *element = selection_target(button->get_position())) {
            element->set_selected(add_selection);
            return true;
        }
        selection_start = selection_end = button->get_position();
        add_to_selection = add_selection;
        for (int32_t index = 0; index < get_child_count(); ++index) {
            auto *element = Object::cast_to<GraphElement>(get_child(index));
            if (element && element->is_selected()) original_selection.insert(element);
        }
        return true;
    }

    /// Update a modified marquee or finish it on button release.
    bool track_selection(const Ref<InputEvent> &event) {
        if (!add_to_selection) return false;
        const Ref<InputEventMouseMotion> motion = event;
        if (motion.is_valid()) {
            if (!motion->get_button_mask().has_flag(MOUSE_BUTTON_MASK_LEFT)) {
                clear_selection_gesture(true);
                return false;
            }
            selection_end = motion->get_position();
        } else {
            const Ref<InputEventMouseButton> button = event;
            if (button.is_null()) return false;
            if (button->get_button_index() == MOUSE_BUTTON_RIGHT && button->is_pressed()) {
                clear_selection_gesture(true);
                return true;
            }
            if (button->get_button_index() != MOUSE_BUTTON_LEFT || button->is_pressed()) return false;
            selection_end = button->get_position();
        }
        const Rect2 box = Rect2(selection_start, selection_end - selection_start).abs();
        for (int32_t index = 0; index < get_child_count(); ++index) {
            auto *element = Object::cast_to<GraphElement>(get_child(index));
            if (!element || !element->is_visible()) continue;
            const Rect2 rect(element->get_position(), element->get_size() * get_zoom());
            const bool inside = element->is_class("GraphFrame") ? box.encloses(rect) : box.intersects(rect);
            element->set_selected(inside ? *add_to_selection : original_selection.count(element) != 0);
        }
        if (motion.is_null()) clear_selection_gesture(false);
        interaction_overlay->queue_redraw();
        return true;
    }

    /// Draw the selection rectangle and a compact compatibility check near the socket.
    void draw_interaction_overlay() {
        if (add_to_selection) {
            const Rect2 box = Rect2(selection_start, selection_end - selection_start).abs();
            interaction_overlay->draw_rect(box, get_theme_color("selection_fill"));
            interaction_overlay->draw_rect(box, get_theme_color("selection_stroke"), false, 1.0f);
        }
        const float scale = ui::pixels(this, 100) * 0.01f;
        if (picker_wire) {
            Vector2 from;
            Vector2 to;
            Color color;
            if (picker_wire(from, to, color) && !from.is_equal_approx(to)) {
                interaction_overlay->draw_polyline(_get_connection_line(from, to), color, 3.0f * scale, true);
            }
        }
        if (dragged_pointer && dragged_socket_node) {
            const int32_t count = dragged_socket_output ? dragged_socket_node->get_output_port_count()
                                                        : dragged_socket_node->get_input_port_count();
            if (dragged_socket_port >= 0 && dragged_socket_port < count) {
                const Vector2 socket = dragged_socket_output ? dragged_socket_node->get_output_port_position(dragged_socket_port)
                                                             : dragged_socket_node->get_input_port_position(dragged_socket_port);
                const Vector2 fixed = dragged_socket_node->get_position() + socket * get_zoom();
                const Vector2 moving = compatible_socket.value_or(*dragged_pointer);
                if (!fixed.is_equal_approx(moving)) {
                    const auto points = dragged_socket_output ? _get_connection_line(fixed, moving) : _get_connection_line(moving, fixed);
                    const Color type = dragged_socket_output ? dragged_socket_node->get_output_port_color(dragged_socket_port)
                                                             : dragged_socket_node->get_input_port_color(dragged_socket_port);
                    interaction_overlay->draw_polyline(points, type.lerp(Color(1, 1, 1), compatible_socket ? 0.22f : 0.06f),
                            3.0f * scale, true);
                }
            }
        }
        if (!compatible_socket) return;
        const float radius = 8.0f * scale;
        const Vector2 safe_margin(radius + 2.0f, radius + 2.0f);
        const Vector2 center = (*compatible_socket + Vector2(dragged_socket_output ? -16.0f : 16.0f, 15.0f) * scale)
                                       .clamp(safe_margin, (get_size() - safe_margin).max(safe_margin));
        interaction_overlay->draw_circle(center, radius, Color::html("#16191b"), true, -1.0f, true);
        const Color green = Color::html("#80d9a3");
        interaction_overlay->draw_circle(center, radius - 1.0f, green, false, scale, true);
        PackedVector2Array check;
        for (const Vector2 offset : {Vector2(-3.5f, 0), Vector2(-1.1f, 2.3f), Vector2(3.6f, -2.6f)}) {
            check.append(center + offset * scale);
        }
        interaction_overlay->draw_polyline(check, green, 1.5f * scale, true);
    }

protected:
    /// The editor canvas needs no additional script bindings.
    static void _bind_methods() {}

    /// Render tentative connections on the canvas without adding graph edges.
    void _notification(int what) {
        if (what == NOTIFICATION_WM_WINDOW_FOCUS_OUT || what == NOTIFICATION_APPLICATION_FOCUS_OUT ||
                (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree())) {
            stop_pan_hover();
        }
        if (what == NOTIFICATION_DRAG_END) {
            set_compatible_socket({});
        }
        if ((what == NOTIFICATION_WM_WINDOW_FOCUS_OUT || what == NOTIFICATION_APPLICATION_FOCUS_OUT) && cancel_pointer_gestures) {
            cancel_pointer_gestures();
        }
        if ((what == NOTIFICATION_WM_WINDOW_FOCUS_OUT || what == NOTIFICATION_APPLICATION_FOCUS_OUT ||
                    what == NOTIFICATION_WM_MOUSE_EXIT) && pin_pointer) {
            pin_pointer({});
        }
        if ((what == NOTIFICATION_WM_WINDOW_FOCUS_OUT || what == NOTIFICATION_APPLICATION_FOCUS_OUT ||
                    what == NOTIFICATION_WM_WINDOW_FOCUS_IN || what == NOTIFICATION_APPLICATION_FOCUS_IN) && window_state_changed) {
            window_state_changed();
        }
        if (what != NOTIFICATION_DRAW) return;
        for (const auto &[from, to] : wire_preview) {
            draw_polyline(_get_connection_line(from, to), Color(0.58, 0.65, 0.94, 0.7), 2.0, true);
        }
    }

public:
    /// Resolve the picker-held wire against the current socket and popup geometry.
    std::function<bool(Vector2 &, Vector2 &, Color &)> picker_wire;
    /// Forward accepted scene and script drops to the workspace's insertion flow.
    std::function<void(Vector2, const Array &)> drop_symbols;
    /// Place a file without loading resources while Godot tests drag compatibility.
    std::function<void(Vector2, const String &)> drop_file;
    /// Let graph inputs handle drops directly and create nodes only on blank canvas.
    std::function<bool(Vector2)> can_drop_file;
    /// Resolve native Scene dock drags to references relative to the graph owner.
    std::function<Dictionary(const Dictionary &)> scene_dock_reference;
    /// Update the hovered socket for a declaration drag independently of native wire drags.
    std::function<void(Vector2, const Array &)> preview_symbol_drop;
    /// Share pointer geometry between socket and label hover without changing native hit targets.
    std::function<void(std::optional<Vector2>)> pin_pointer;
    /// Insert a dot when a native connection is double-clicked.
    std::function<void(Vector2)> add_reroute;
    /// Let the workspace consume explicitly reserved wire gestures.
    std::function<bool(const Ref<InputEvent> &)> wire_input;
    /// Pause input that reaches the canvas before Control's modal filtering.
    std::function<bool()> input_blocked;
    /// Reserve modified pin presses before the native connection overlay consumes them.
    std::function<bool(const Ref<InputEvent> &)> early_wire_input;
    /// Advance edge panning while a wire is active.
    std::function<void(double)> wire_pan;
    /// Abandon workbench pointer gestures when the graph or application loses focus.
    std::function<void()> cancel_pointer_gestures;
    /// Refresh the native wire projection after a window focus transition.
    std::function<void()> window_state_changed;
    /// Resolve the value on a native graph socket without intercepting its input.
    std::function<String(Vector2)> debugger_tooltip;
    /// Draw curved, straight, or right-angle connections using native hit geometry.
    int32_t wire_style = 0;
    /// Temporary on-screen edges drawn while a gesture is in progress.
    std::vector<std::pair<Vector2, Vector2>> wire_preview;

    /// Respect editable fields on a node and native canvas controls, including internal scrollbars.
    bool interactive_control_at(Vector2 point) {
        auto *hover = get_window()->gui_get_hovered_control();
        if (hover && hover->is_class("ScrollBar") && is_ancestor_of(hover) && hover->get_global_rect().has_point(get_global_transform().xform(point))) {
            return true;
        }
        if (auto *element = selection_target(point)) {
            const Vector2 local = element->get_transform().affine_inverse().xform(point);
            for (int32_t index = element->get_child_count() - 1; index >= 0; --index) {
                auto *child = Object::cast_to<Control>(element->get_child(index));
                if (child && interactive_at(child, child->get_transform().affine_inverse().xform(local))) return true;
            }
        }
        return false;
    }

    /// Install the feedback and pan-hover layers after adding the graph to the editor.
    void build_interaction_overlay() {
        interaction_overlay = add<Control>(this);
        interaction_overlay->set_name("GraphInteractionOverlay");
        interaction_overlay->set_z_index(20);
        interaction_overlay->set_mouse_filter(MOUSE_FILTER_IGNORE);
        interaction_overlay->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
        interaction_overlay->connect("draw", callable_mp(this, &VisualScriptCanvas::draw_interaction_overlay));

        pan_hover = add<Control>(this);
        pan_hover->set_name("GraphPanHover");
        pan_hover->set_as_top_level(true);
        pan_hover->set_mouse_filter(MOUSE_FILTER_PASS);
        pan_hover->hide();
        connect("item_rect_changed", callable_mp(this, &VisualScriptCanvas::fit_pan_hover));
    }

    /// Redraw the connection attached to the picker on open, scroll, or dismissal.
    void refresh_picker_wire() {
        interaction_overlay->queue_redraw();
    }

    /// Drop the marquee and optionally restore the pre-gesture selection.
    void clear_selection_gesture(bool restore) {
        if (!add_to_selection) return;
        add_to_selection.reset();
        if (restore) {
            for (int32_t index = 0; index < get_child_count(); ++index) {
                auto *element = Object::cast_to<GraphElement>(get_child(index));
                if (element) element->set_selected(original_selection.count(element) != 0);
            }
        }
        original_selection.clear();
        interaction_overlay->queue_redraw();
    }

    /// Redraw when compatibility or the target socket changes.
    void set_compatible_socket(std::optional<Vector2> socket) {
        if (compatible_socket == socket) return;
        compatible_socket = socket;
        interaction_overlay->queue_redraw();
    }

    /// Keep the overlay aligned with the source for the life of this drag.
    void set_dragged_socket(GraphNode *node, int32_t port, bool output) {
        dragged_socket_node = node;
        dragged_socket_port = port;
        dragged_socket_output = output;
        dragged_pointer.reset();
        set_compatible_socket({});
        interaction_overlay->queue_redraw();
    }

    /// Draw the open end at the pointer or at a confirmed compatible socket.
    void update_dragged_wire(Vector2 point, std::optional<Vector2> target) {
        dragged_pointer = point;
        set_compatible_socket(target);
        interaction_overlay->queue_redraw();
    }

    /// Draw continuous screen-space curves and preserve the same geometry for wire hit tests.
    PackedVector2Array _get_connection_line(const Vector2 &from, const Vector2 &to) const override {
        PackedVector2Array points;
        if (wire_style == 1) {
            points.append(from);
            points.append(to);
        } else if (wire_style == 2) {
            const real_t middle = (from.x + to.x) * .5f;
            points.append(from);
            points.append(Vector2(middle, from.y));
            points.append(Vector2(middle, to.y));
            points.append(to);
        } else {
            const real_t distance = from.distance_to(to);
            const real_t bend = std::max<real_t>(24.0, std::max(std::abs(to.x - from.x) * 0.42, std::abs(to.y - from.y) * 0.30));
            const int32_t segments = std::clamp(static_cast<int32_t>(distance * 0.12), 16, 96);
            points.resize(segments + 1);
            for (int32_t index = 0; index <= segments; ++index) {
                points.set(index, from.bezier_interpolate(from + Vector2(bend, 0), to - Vector2(bend, 0), to,
                        static_cast<real_t>(index) / segments));
            }
        }
        return points;
    }

    /// Convert editor symbol payloads to the workspace's existing insertion options.
    Array drop_options(const Variant &data) const {
        if (data.get_type() != Variant::DICTIONARY || (input_blocked && input_blocked())) {
            return {};
        }
        const Dictionary drag = data;
        const String type = drag.get("type", String());
        if (type == "crimson_visual_script_symbol") {
            return drag.get("options", Array());
        }
        if (type == "nodes" && scene_dock_reference) {
            const Dictionary reference = scene_dock_reference(drag);
            if (!reference.is_empty()) {
                return Array::make(record_from("node", reference));
            }
        }
        return {};
    }

    bool _can_drop_data(const Vector2 &point, const Variant &data) const override {
        if (input_blocked && input_blocked()) return false;
        const String path = dropped_project_file(data);
        if (!path.is_empty()) {
            if (!can_drop_file || !can_drop_file(point)) return false;
            const StringName type = EditorInterface::get_singleton()->get_resource_filesystem()->get_file_type(path);
            return ClassDB::is_parent_class(type, "Resource");
        }
        return !drop_options(data).is_empty();
    }

    /// Give the workspace typed insertion options at the drop position.
    void _drop_data(const Vector2 &point, const Variant &data) override {
        const String path = dropped_project_file(data);
        if (!path.is_empty() && drop_file && _can_drop_data(point, data)) {
            drop_file(point, path);
            return;
        }
        if (drop_symbols) {
            const Array options = drop_options(data);
            if (!options.is_empty()) drop_symbols(point, options);
        }
    }

    /// Keep declaration drops out of native canvas gestures and handle wire double-clicks.
    void _gui_input(const Ref<InputEvent> &event) override {
        if (input_blocked && input_blocked()) {
            accept_event();
            return;
        }
        const Ref<InputEventMouseButton> button = event;
        if (button.is_valid() && button->is_pressed() && button->get_button_index() == MOUSE_BUTTON_LEFT &&
                _can_drop_data(button->get_position(), get_window()->gui_get_drag_data())) {
            // The Viewport still handles the drop after GUI input. GraphEdit must not start a node move.
            accept_event();
            return;
        }
        if (add_reroute && button.is_valid() && button->is_pressed() && button->is_double_click() &&
                button->get_button_index() == MOUSE_BUTTON_LEFT && !get_closest_connection_at_point(button->get_position(), 8.0).is_empty()) {
            add_reroute(button->get_position());
            accept_event();
        } else if (wire_input && wire_input(event)) {
            accept_event();
        } else if (button.is_valid() && button->is_pressed() && button->get_button_index() == MOUSE_BUTTON_MIDDLE &&
                !button->is_alt_pressed() && !button->is_ctrl_pressed() &&
                static_cast<int64_t>(button->get_button_mask()) == MOUSE_BUTTON_MASK_MIDDLE) {
            // The viewport already captured the press. Let GraphEdit pan as usual and keep
            // hover hit testing away from every card and inline field while the button is held.
            fit_pan_hover();
            pan_hover->show();
        }
    }

    /// Handle tracked marquees and reserve modified wire gestures before Godot's native input.
    void _input(const Ref<InputEvent> &event) override {
        if (!is_visible_in_tree() || (input_blocked && input_blocked())) {
            stop_pan_hover();
            return;
        }
        const Ref<InputEvent> local = make_input_local(event);
        const Ref<InputEventKey> key = local;
        const Ref<InputEventMouseMotion> motion = local;
        const Ref<InputEventMouseButton> button = local;
        if (pan_hover && pan_hover->is_visible() && (button.is_valid() || local->is_action_pressed("ui_cancel") ||
                (motion.is_valid() && !motion->get_button_mask().has_flag(MOUSE_BUTTON_MASK_MIDDLE)))) {
            stop_pan_hover();
        }
        if (motion.is_valid() && pin_pointer) {
            pin_pointer(static_cast<int64_t>(motion->get_button_mask()) == 0 ?
                    std::optional<Vector2>(motion->get_position()) : std::nullopt);
        }
        if (preview_symbol_drop && (motion.is_valid() || (key.is_valid() && key->get_keycode() == KEY_CTRL))) {
            const Variant drag = get_window()->gui_get_drag_data();
            if (drag.get_type() == Variant::DICTIONARY && String(Dictionary(drag).get("type", String())) == "crimson_visual_script_symbol") {
                preview_symbol_drop(motion.is_valid() ? motion->get_position() : get_local_mouse_position(),
                        Dictionary(drag).get("options", Array()));
            }
        }
        if (track_selection(local) || (early_wire_input && early_wire_input(local)) ||
                (button.is_valid() && Rect2(Vector2(), get_size()).has_point(button->get_position()) &&
                        get_window()->gui_get_drag_data().get_type() == Variant::NIL && !interactive_control_at(button->get_position()) &&
                        start_selection(button))) {
            get_viewport()->set_input_as_handled();
        }
    }

    /// Run continuous panning only while a wire gesture needs it.
    void _process(double delta) override {
        if (is_visible_in_tree() && wire_pan && !(input_blocked && input_blocked())) wire_pan(delta);
    }

    /// Native port overlays pass unhandled tooltips to this canvas.
    String _get_tooltip(const Vector2 &point) const override {
        return debugger_tooltip ? debugger_tooltip(point) : String();
    }

    /// Avoid snapping to data ports whose native types cannot be assigned.
    bool _is_node_hover_valid(const StringName &from, int32_t from_port, const StringName &to, int32_t to_port) override {
        auto *source = Object::cast_to<GraphNode>(get_node_or_null(NodePath(String(from))));
        auto *target = Object::cast_to<GraphNode>(get_node_or_null(NodePath(String(to))));
        return compatible_ports(source, from_port, target, to_port);
    }

};

/// Render round data sockets and a clearly shaped execution socket.
class VisualScriptGraphNode final : public GraphNode {
    GDCLASS(VisualScriptGraphNode, GraphNode)

    /// Keep the card outline crisp when changing zoom.
    double last_zoom = 1.0;
    /// Reuse the shared card frame for both normal and typed outlines.
    Ref<StyleBoxFlat> card_outline;
    /// A variable's value type colors its idle outline.
    std::optional<Color> variable_border;
    /// Native input slots with a saved wire.
    std::set<int32_t> connected_inputs;
    /// Native output slots with one or more saved wires.
    std::set<int32_t> connected_outputs;
    /// Slot and side of the pin under the pointer.
    std::optional<std::pair<int32_t, bool>> hovered_pin;
    /// Keep debugger state in the editor widget rather than in the graph resource.
    bool has_breakpoint = false;
    bool debug_paused = false;
    /// Draw the code marker without a container slot or input target.
    Ref<StyleBoxFlat> code_badge;
    /// Stock editor glyph drawn inside the marker.
    Ref<Texture2D> code_badge_icon;
    /// Reuse the passive paused pill while this card owns the active instruction.
    Ref<StyleBoxFlat> paused_badge;
    /// Retain non-scalar glyphs and optional second colors by native row and side.
    std::map<std::pair<int32_t, bool>, PinAppearance> presented_pins;
    /// Repaint an existing role glyph without rebuilding a live literal field.
    TextureRect *constructor_icon = nullptr;

    /// Find the visible caption used for hover, independently of action hit testing.
    Label *hover_caption(const HBoxContainer *row, bool output, bool execution) const {
        if (!row) return nullptr;
        const char *direction = execution ? flow_hover_label_direction : pin_label_direction;
        for (int32_t index = 0; index < row->get_child_count(); ++index) {
            auto *label = Object::cast_to<Label>(row->get_child(index));
            if (label && label->is_visible() && label->has_meta(direction) && bool(label->get_meta(direction)) == output) return label;
        }
        return nullptr;
    }

    /// Bound the socket-to-caption stripe so it never covers the opposite side or a different row.
    Rect2 pin_hover_area(int32_t slot, bool left, const Vector2 &point, bool execution) {
        const auto *row = Object::cast_to<HBoxContainer>(get_child(slot));
        if (!row) return {};
        const float padding = ui::pixels(this, ui::spacing::xs);
        const float sign = left ? 1.0f : -1.0f;
        float start = point.x - sign * (padding + ui::pixels(this, 3));
        float end = point.x + sign * (padding + ui::pixels(this, 12));
        if (const auto *label = hover_caption(row, !left, execution)) {
            const Rect2 bounds = label->get_rect();
            end = row->get_position().x + (left ? bounds.get_end().x : bounds.position.x) + sign * padding;
        }
        start = std::clamp(start, 1.0f, get_size().x - 1.0f);
        end = std::clamp(end, 1.0f, get_size().x - 1.0f);
        const float height = std::min(row->get_size().y, static_cast<float>(ui::pixels(this, 22)));
        return Rect2(std::min(start, end), point.y - height * 0.5f, std::abs(end - start), height);
    }

    /// Paint a linear fade from the socket edge inward through its caption.
    void draw_pin_hover(int32_t slot, bool left, const Vector2 &point, bool execution, Color color) {
        const Rect2 area = pin_hover_area(slot, left, point, execution);
        if (!area.has_area()) return;
        const float start = left ? area.position.x : area.get_end().x;
        const float end = left ? area.get_end().x : area.position.x;
        const float top = area.position.y;
        const float bottom = area.get_end().y;
        color.a = 0.24f;
        Color fade = color;
        fade.a = 0.045f;
        draw_polygon(PackedVector2Array{Vector2(start, top), Vector2(end, top), Vector2(end, bottom), Vector2(start, bottom)},
                PackedColorArray{color, fade, fade, color});
    }

protected:
    /// The node only customizes native GraphNode rendering.
    static void _bind_methods() {}

    /// Draw one card outline in screen-scaled width, preserving the port layer.
    void _notification(int what) {
        if (what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
            const double zoom = get_scale().x;
            if (zoom != last_zoom) {
                last_zoom = zoom;
                set_pin_hover({});
                queue_redraw();
            }
        } else if (what == NOTIFICATION_DRAW && !has_meta(routing_dot_key)) {
            const double zoom = std::max(static_cast<double>(get_scale().x), 0.01);
            const bool selected = is_selected();
            const float width = static_cast<float>((selected || debug_paused ? 2.0 : 1.0) / std::min(zoom, 1.0));
            const Color keyline = debug_paused ? ui::graph_node_paused_outline() :
                    ui::color(selected ? ui::Surface::HIGHLIGHT : ui::Surface::OUTLINE);
            if (variable_border) {
                ui::draw_graph_node_outline(this, card_outline, *variable_border, false, zoom);
                if (selected || debug_paused) {
                    card_outline->set_border_width_all(std::ceil(width));
                    card_outline->set_border_color(keyline);
                    const real_t gap = 2.0 / zoom;
                    draw_style_box(card_outline, Rect2(Vector2(-gap, -gap), get_size() + Vector2(2 * gap, 2 * gap)));
                }
            } else {
                ui::draw_graph_node_outline(this, card_outline, keyline, selected || debug_paused, zoom);
            }
            if (has_breakpoint) {
                const real_t radius = std::max<real_t>(ui::pixels(this, 5), 4.0 / zoom);
                const Vector2 marker(0, 0);
                draw_circle(marker, radius + 1.5 / zoom, Color::html("#222225"), true, -1, true);
                draw_circle(marker, radius, Color::html("#ff5874"), true, -1, true);
            }
            if (code_badge.is_valid()) {
                const real_t side = std::max<real_t>(ui::pixels(this, 16), ui::pixels(this, 11) / zoom);
                const Vector2 size(side, side);
                const Vector2 corner(get_size().x, 0);
                const Rect2 badge(corner - size * 0.5f, size);
                draw_style_box(code_badge, badge);
                if (code_badge_icon.is_valid()) {
                    draw_texture_rect(code_badge_icon, badge.grow(-side * 0.16f), false, Color::html("#d7e8fc"));
                }
            }
            if (debug_paused) {
                ui::draw_graph_node_paused(this, paused_badge);
            }
        }
    }

public:
    /// Apply the resolved appearance in place and update collection constructors' matching title marker.
    void present_pin(int32_t slot, bool output, const PinAppearance &appearance, bool collection_constructor) {
        const auto socket = std::pair(slot, output);
        if (appearance.shape == ui::GraphValueShape::SCALAR) presented_pins.erase(socket);
        else presented_pins.insert_or_assign(socket, appearance);
        if (output) {
            set_slot_color_right(slot, appearance.ink);
            if (collection_constructor) {
                const auto marker = ui::graph_type_marker(this, appearance.shape, appearance.ink, appearance.secondary, 12);
                if (constructor_icon) {
                    constructor_icon->set_texture(marker);
                } else {
                    const Color neutral = ui::graph_pin_color(Variant::NIL);
                    constructor_icon = ui::style_graph_node_title_icon(this, marker, neutral, Color(1, 1, 1));
                }
            }
        } else {
            set_slot_color_left(slot, appearance.ink);
        }
        queue_redraw();
    }

    /// Use the same frame geometry as the shared graph style and catalog.
    void style_card(bool compact_read) {
        card_outline = ui::graph_node_outline(this, compact_read ? ui::graph_compact_read_radius : ui::graph_node_corner_radius);
    }

    /// Enable the upper-right code marker on a Custom Script card.
    void show_custom_script_badge() {
        code_badge = ui::box(ui::color(ui::Surface::RAISED), ui::color(ui::Surface::HIGHLIGHT),
                0, 0, get_theme_default_base_scale());
        code_badge->set_corner_radius_all(ui::pixels(this, 4));
        code_badge_icon = get_theme_icon("Script", "EditorIcons");
        queue_redraw();
    }

    /// Use the same type color as the variable's value pin.
    void set_variable_border(const Color &color) {
        variable_border = color;
    }

    /// Keep the socket fill in sync with the connections displayed on the canvas.
    void mark_connected(int32_t port, bool output, bool connected = true) {
        auto &slots = output ? connected_outputs : connected_inputs;
        const int32_t slot = output ? get_output_port_slot(port) : get_input_port_slot(port);
        if (connected ? slots.insert(slot).second : slots.erase(slot) != 0) {
            queue_redraw();
        }
    }

    /// Update one socket and its caption together without changing the pin's hit area.
    void set_pin_hover(std::optional<std::pair<int32_t, bool>> pin) {
        if (hovered_pin == pin) return;
        const auto tint_label = [this](const std::pair<int32_t, bool> &highlight, bool hovered) {
            const auto *row = Object::cast_to<HBoxContainer>(get_child(highlight.first));
            const bool execution = (highlight.second ? get_slot_type_right(highlight.first) : get_slot_type_left(highlight.first)) == EXECUTION;
            if (auto *label = hover_caption(row, highlight.second, execution)) {
                if (hovered) label->add_theme_color_override("font_color", ui::color(ui::Surface::TEXT));
                else label->remove_theme_color_override("font_color");
            }
        };
        if (hovered_pin) tint_label(*hovered_pin, false);
        hovered_pin = pin;
        if (hovered_pin) tint_label(*hovered_pin, true);
        queue_redraw();
    }

    /// Resolve a flow port by its painted stripe while leaving native socket actions unchanged.
    std::optional<std::pair<int32_t, bool>> execution_port_at(const Vector2 &point) {
        if (has_meta(routing_dot_key)) return {};
        for (const bool output : {false, true}) {
            const int32_t count = output ? get_output_port_count() : get_input_port_count();
            for (int32_t port = 0; port < count; ++port) {
                if ((output ? get_output_port_type(port) : get_input_port_type(port)) != EXECUTION) continue;
                const int32_t slot = output ? get_output_port_slot(port) : get_input_port_slot(port);
                const Vector2 socket = output ? get_output_port_position(port) : get_input_port_position(port);
                if (pin_hover_area(slot, !output, socket, true).has_point(point)) return std::pair(port, output);
            }
        }
        return {};
    }

    /// Retain the card and update its debugger decoration independently of graph edits.
    void set_debug_state(bool breakpoint, bool paused) {
        if (has_breakpoint == breakpoint && debug_paused == paused) return;
        has_breakpoint = breakpoint;
        debug_paused = paused;
        if (paused && paused_badge.is_null()) {
            paused_badge = ui::graph_node_paused_badge(this);
        }
        queue_redraw();
    }

    /// Show a paused value when the engine resolves hover directly to the node card.
    String _get_tooltip(const Vector2 &point) const override {
        const auto *workspace = Object::cast_to<VisualScriptCanvas>(get_parent());
        const String value = workspace && workspace->debugger_tooltip ?
                workspace->debugger_tooltip(get_transform().xform(point)) : String();
        return value.is_empty() ? get_tooltip_text() : value;
    }

    /// Route declaration drops through the same canvas path when the pointer hits a pin label.
    bool _can_drop_data(const Vector2 &point, const Variant &data) const override {
        const auto *workspace = Object::cast_to<VisualScriptCanvas>(get_parent());
        return workspace && workspace->_can_drop_data(get_transform().xform(point), data);
    }

    /// Forward one drop in canvas-local coordinates.
    void _drop_data(const Vector2 &point, const Variant &data) override {
        if (auto *workspace = Object::cast_to<VisualScriptCanvas>(get_parent())) {
            workspace->_drop_data(get_transform().xform(point), data);
        }
    }

    /// Draw the pin by role at the same position used for Godot's native hit zone.
    void _draw_port(int32_t slot, const Vector2i &position, bool left, const Color &color) override {
        const Vector2 point = position;
        const double zoom = std::max(static_cast<double>(get_scale().x), 0.01);
        if (has_meta(routing_dot_key)) {
            if (left) {
                const Vector2 center(get_size().x * 0.5f, point.y);
                const float radius = static_cast<float>(std::max(static_cast<double>(ui::pixels(this, 5)), 3.0 / zoom));
                draw_circle(center, radius + 1.0 / zoom, Color::html("#18181b"), true, -1, true);
                draw_circle(center, radius, color, true, -1, true);
                if (is_selected()) {
                    draw_circle(center, radius + 1.0 / zoom, Color::html("#c9d8fc"), false, 1.5 / zoom, true);
                }
            }
            return;
        }
        const auto &slots = left ? connected_inputs : connected_outputs;
        const bool execution = (left ? get_slot_type_left(slot) : get_slot_type_right(slot)) == EXECUTION;
        const bool hovered = hovered_pin == std::pair(slot, !left);
        const auto appearance = presented_pins.find(std::pair(slot, !left));
        const auto shape = appearance != presented_pins.end() ? appearance->second.shape : ui::GraphValueShape::SCALAR;
        std::optional<Color> secondary = appearance != presented_pins.end() ? appearance->second.secondary : std::nullopt;
        if (hovered && secondary) *secondary = secondary->lightened(0.12f);
        if (hovered) draw_pin_hover(slot, left, point, execution, execution ? ui::color(ui::Surface::MUTED) : color);
        const Color ink = hovered ? color.lightened(0.12f) : color;
        ui::draw_graph_pin(this, point, ink, execution, slots.count(slot) != 0, zoom, shape, secondary);
    }
};

/// Return the Godot value or class label shown on a pin and in its help.
String type_label(const Dictionary &pin, const Dictionary &document = {}) {
    if (is_execution(pin)) {
        return "Execution";
    }
    if (pin.get("iteration_collection", false) && !pin.get("iteration_shape_known", false)) {
        return "Array or Dictionary";
    }
    const String script_name = pin.get("script_name", String());
    if (!script_name.is_empty()) {
        return script_name;
    }
    const StringName class_name = pin.get("class", StringName());
    if (!class_name.is_empty()) {
        return String(class_name);
    }
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", Variant::NIL)));
    if (type == Variant::ARRAY && pin.has("element")) {
        return "Array[" + type_label(pin["element"], document) + "]";
    }
    if (type == Variant::DICTIONARY && (pin.has("key") || pin.has("value"))) {
        return "Dictionary[" + type_label(pin.get("key", Dictionary()), document) + ", " +
                type_label(pin.get("value", Dictionary()), document) + "]";
    }
    const String enum_type = pin.get("enum_type", StringName());
    if (!enum_type.is_empty() && !enum_type.begins_with("GraphEnum.")) {
        return enum_type;
    }
    if (!enum_type.is_empty()) {
        const String name = authored_enum_label(document, StringName(enum_type));
        return name.is_empty() ? String("Enum") : name;
    }
    return type == Variant::NIL ? String("Variant") : Variant::get_type_name(type);
}

/// Distinguish the optional script type from the Godot class of a scene reference.
String reference_type_label(const Dictionary &pin, const Dictionary &document) {
    const String type = type_label(pin, document);
    const String native_class = pin.get("class", StringName());
    return native_class.is_empty() || native_class == type ? type : native_class + String(" (") + type + String(")");
}

/// Keep the pin glyph, wire tint, and label help in sync without changing the card's controls.
void present_node_pins(VisualScriptGraphNode *widget, const Dictionary &document, const GraphPinLayout &presentation,
        bool collection_constructor, bool refresh_contract = false) {
    for (const bool output : {false, true}) {
        const Array visible = output ? presentation.outputs : presentation.inputs;
        const Array real = refresh_contract ? (output ? presentation.contract_outputs : presentation.contract_inputs) : Array();
        int64_t index = 0;
        for (int32_t slot = 0; slot < widget->get_child_count(); ++slot) {
            if (!(output ? widget->is_slot_enabled_right(slot) : widget->is_slot_enabled_left(slot))) continue;
            Dictionary pin = output ? widget->get_slot_metadata_right(slot) : widget->get_slot_metadata_left(slot);
            const StringName id = pin.get("id", StringName());
            const Dictionary actual = index < real.size() ? Dictionary(real[index]) : pin;
            if (actual.get("id", StringName()) == id && actual != pin) {
                pin = actual;
                if (output) widget->set_slot_metadata_right(slot, pin);
                else widget->set_slot_metadata_left(slot, pin);
            }
            const Dictionary projected = index < visible.size() ? Dictionary(visible[index]) : pin;
            const Dictionary &display = projected.get("id", StringName()) == id ? projected : pin;
            ++index;
            if (is_execution(pin)) continue;
            auto *row = Object::cast_to<HBoxContainer>(widget->get_child(slot));
            if (!row) continue;
            const char *view_key = output ? output_pin_presentation : input_pin_presentation;
            if (row->has_meta(view_key) && row->get_meta(view_key) == display) continue;
            row->set_meta(view_key, display);
            widget->present_pin(slot, output, pin_appearance(display), collection_constructor);

            const String caption = row->get_meta(output ? output_pin_caption : input_pin_caption, String());
            const String help = output ? String() : String(row->get_meta(input_pin_help, String()));
            const String type = output && bool(row->get_meta(reference_pin_caption, false)) ? reference_type_label(display, document) :
                    type_label(display, document);
            String hint = caption.is_empty() ? type : caption + String("\n") + type;
            if (display.get("nullable", false)) hint += String("\nMay be null.");
            if (!help.is_empty()) hint += String("\n\n") + help;
            row->set_meta(output ? output_socket_hint : input_socket_hint, hint);
            for (int32_t child = 0; child < row->get_child_count(); ++child) {
                auto *label = Object::cast_to<Label>(row->get_child(child));
                if (label && label->has_meta(pin_label_direction) && bool(label->get_meta(pin_label_direction)) == output) {
                    label->set_tooltip_text(hint);
                    label->set_meta(pin_label_hint, hint);
                    break;
                }
            }
        }
    }
}

/// Pick a concise visual label while retaining the native signature in the descriptor.
String member_title(const StringName &kind, const Dictionary &member) {
    const String name = String(member.get("name", StringName())).capitalize();
    if (kind == StringName(call_method_kind) && StringName(member.get("class", StringName())) == StringName("GameplayTag") &&
            StringName(member.get("name", StringName())) == StringName("find")) {
        return "Find Tag";
    }
    if (kind == StringName(get_property_kind)) {
        return "Get " + name;
    }
    if (kind == StringName(set_property_kind)) {
        return "Set " + name;
    }
    return name;
}

/// Group the declared tag methods by role in the Gameplay Tags category.
String gameplay_tag_scope(const StringName &owner) {
    if (owner == StringName("GameplayTag")) return "Tag";
    if (owner == StringName("GameplayTagContainer")) return "Container";
    if (owner == StringName("GameplayTagCatalog")) return "Catalog";
    if (owner == StringName("GameplayTagQuery")) return "Query";
    return {};
}

/// Use Godot's const flag for the reflected method indicator.
bool is_const_method(const Dictionary &member) {
    if (member.has("const")) {
        return member["const"];
    }
    const StringName owner = member.get("class", StringName());
    const StringName name = member.get("name", StringName());
    if (owner.is_empty() || name.is_empty()) {
        return false;
    }
    for (const Dictionary &method : ClassDB::class_get_method_list(owner, true)) {
        if (StringName(method.get("name", StringName())) == name) {
            return (static_cast<int64_t>(method.get("flags", 0)) & METHOD_FLAG_CONST) != 0;
        }
    }
    return false;
}

/// Distinguish read-only native methods from calls that may modify state.
Color method_color(const Dictionary &member) {
    return is_const_method(member) ? Color::html("#83c89b") : Color::html("#d994b9");
}

/// Resolve the authored symbol that supplies a graph card's name and description.
Dictionary card_declaration(const Dictionary &document, const Dictionary &graph, const Dictionary &node, const String &kind) {
    if (kind == "get_variable" || kind == "set_variable") {
        const bool local = String(node.get("scope", String())) == "local";
        return find_graph_record(graph_array(local ? graph : document, local ? "locals" : "variables"), node.get("variable", 0));
    }
    if (kind == "call_function" || kind == "call_macro" || kind == "custom_event" || kind == "emit_event") {
        const char *array = kind == "call_function" ? "functions" : kind == "call_macro" ? "macros" : "events";
        return find_graph_record(graph_array(document, array), node.get("definition", 0));
    }
    return {};
}

/// Label an authored card with its symbol's name and how this card uses it.
String declared_name(const Dictionary &declaration, const String &kind) {
    if (kind == "get_variable" || kind == "set_variable") {
        return (kind == "get_variable" ? String("Get ") : String("Set ")) + String(declaration.get("name", "Variable"));
    }
    if (kind == "call_function" || kind == "call_macro" || kind == "custom_event" || kind == "emit_event") {
        const String compact = declaration.get("compact_title", String());
        const String name = (kind == "call_function" || kind == "call_macro") && !compact.is_empty() ?
                compact : String(declaration.get("name", String()));
        return (kind == "custom_event" ? String("On ") : kind == "emit_event" ? String("Emit ") : String()) +
                (name.is_empty() ? String("Missing Declaration") : name);
    }
    return {};
}

/// Show native method documentation and preserve type information for property accessors.
String member_help(const StringName &kind, const Dictionary &member) {
    const StringName script_name = member.get("script_name", StringName());
    const StringName owner = script_name.is_empty() ? StringName(member.get("class", StringName())) : script_name;
    const StringName name = member.get("name", StringName());
    if (kind == StringName(call_method_kind) && script_name.is_empty()) {
        return visual_script_method_help(owner, name);
    }
    const Dictionary result = member.get("output", Dictionary());
    String help = String(owner) + String(".") + String(name);
    if (!result.is_empty()) {
        help += String(" returns ") + type_label(result);
    }
    return help;
}

/// Find the native index for a named pin on a rebuilt node.
int32_t pin_index(GraphNode *widget, bool output, const StringName &pin) {
    const int32_t count = output ? widget->get_output_port_count() : widget->get_input_port_count();
    for (int32_t index = 0; index < count; ++index) {
        if (pin_id(widget, output, index) == pin) {
            return index;
        }
    }
    return -1;
}

/// Create the persistent edge representation used by the runtime.
Dictionary edge(int64_t from, const StringName &from_pin, int64_t to, const StringName &to_pin) {
    Dictionary result;
    result["from"] = from;
    result["from_pin"] = String(from_pin);
    result["to"] = to;
    result["to_pin"] = String(to_pin);
    return result;
}

/// Allocate the next node ID without reusing a live record.
int64_t next_node_id(const Array &nodes) {
    int64_t id = 1;
    for (const Dictionary &node : nodes) {
        const int64_t existing = node.get("id", 0);
        if (existing == std::numeric_limits<int64_t>::max()) {
            return 0;
        }
        id = std::max(id, existing + 1);
    }
    return id;
}

/// Show both sides of a native slot with the optional input editor before the flexible spacer.
void add_ports(GraphNode *widget, const Dictionary &document, const Dictionary &input, const Dictionary &output,
        const String &extra_input_help = {}, const String &read_caption = {}, const StringName &caption_node = {},
        const std::function<void(HBoxContainer *)> &add_input_editor = {}) {
    const int32_t slot = widget->get_child_count();
    auto *row = add<HBoxContainer>(widget);
    const bool dot = widget->has_meta(routing_dot_key);
    const int row_height = dot ? ui::graph_reroute_height :
            read_caption.is_empty() ? ui::graph_node_port_row_height : ui::graph_compact_read_height;
    row->set_custom_minimum_size(Vector2(0, ui::pixels(widget, row_height)));
    row->add_theme_constant_override("separation", ui::pixels(row, ui::spacing::sm));
    row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    const auto hint = [&](const Dictionary &pin, const String &caption, const String &help) -> String {
        if (pin.is_empty()) return {};
        const String type = (caption_node == StringName("GraphSceneReferenceName") || caption_node == StringName("GraphSingletonName")) ?
                reference_type_label(pin, document) : type_label(pin, document);
        const String text = caption.is_empty() ? type : caption + String("\n") + type;
        return help.is_empty() ? text : text + String("\n\n") + help;
    };
    const String input_hint = hint(input, input.get("label", String()), extra_input_help);
    const String output_hint = hint(output, read_caption.is_empty() ? String(output.get("label", String())) : read_caption, {});
    row->set_meta(input_socket_hint, input_hint);
    row->set_meta(output_socket_hint, output_hint);
    row->set_meta(input_pin_caption, input.get("label", String()));
    row->set_meta(output_pin_caption, read_caption.is_empty() ? String(output.get("label", String())) : read_caption);
    row->set_meta(input_pin_help, extra_input_help);
    row->set_meta(reference_pin_caption,
            caption_node == StringName("GraphSceneReferenceName") || caption_node == StringName("GraphSingletonName"));
    const auto setup_data_label = [](Label *label, bool outgoing) {
        label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        label->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
        label->set_meta(pin_label_direction, outgoing);
        label->set_meta(pin_label_hint, label->get_tooltip_text());
        label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    };
    const auto add_label = [&](const Dictionary &pin, const String &tooltip, bool outgoing) {
        if (pin.is_empty() || dot) {
            return;
        }
        const String caption = pin.get("label", String());
        const bool execution = is_execution(pin);
        if (!ui::show_graph_pin_label(caption, execution)) {
            if (execution) {
                const String direction = outgoing ? "Output: " : "Input: ";
                const String previous = row->get_accessibility_description();
                row->set_accessibility_description(previous.is_empty() ? direction + caption : previous + String(". ") + direction + caption);
            }
            return;
        }
        auto *label = ui::label(row, caption);
        label->set_theme_type_variation(execution ? "GraphFlowPin" : "GraphDataPin");
        label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
        label->set_tooltip_text(tooltip);
        label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        if (execution) label->set_meta(flow_hover_label_direction, outgoing);
        else setup_data_label(label, outgoing);
    };
    add_label(input, input_hint, false);
    if (add_input_editor) {
        add_input_editor(row);
    }
    if (!read_caption.is_empty()) {
        auto *name = ui::label(row, read_caption);
        name->set_name(caption_node);
        name->set_theme_type_variation("GraphCompactRead");
        name->set_tooltip_text(output_hint);
        setup_data_label(name, true);
    }
    auto *space = add<Control>(row);
    space->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    space->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    if (read_caption.is_empty()) {
        add_label(output, output_hint, true);
    }
    if (dot) {
        // GraphEdit lets clickable child controls take priority over its native socket hit zones.
        // PASS keeps the center available for normal node selection and dragging.
        auto *handle = add<Control>(space);
        handle->set_anchors_and_offsets_preset(Control::PRESET_CENTER);
        const float grip_width = ui::pixels(widget, 8);
        handle->set_offset(SIDE_LEFT, -grip_width);
        handle->set_offset(SIDE_RIGHT, grip_width);
        handle->set_anchor_and_offset(SIDE_TOP, 0, 0);
        handle->set_anchor_and_offset(SIDE_BOTTOM, 1, 0);
        handle->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        handle->set_default_cursor_shape(Control::CURSOR_MOVE);
        handle->set_tooltip_text("Drag the dot to move the wire.");
    }
    widget->set_slot(slot, !input.is_empty(), is_execution(input) ? EXECUTION : DATA, pin_color(input),
            !output.is_empty(), is_execution(output) ? EXECUTION : DATA, pin_color(output), {}, {}, false);
    widget->set_slot_metadata_left(slot, input);
    widget->set_slot_metadata_right(slot, output);
}

/// Keep a literal beside its caption. Long values scroll inside text fields or use their picker.
void size_pin_editor(Control *editor, int minimum_width, int maximum_width) {
    editor->set_custom_minimum_size(Vector2(ui::pixels(editor, minimum_width), ui::pixels(editor, ui::graph_literal_height)));
    editor->set_custom_maximum_size(Vector2(ui::pixels(editor, maximum_width), -1));
    editor->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
    editor->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
}

/// Style a one-line pin editor and let its visible text determine the width within the given bounds.
void style_pin_text(LineEdit *entry, int minimum_width, int maximum_width) {
    ui::style_graph_inline_control(entry, "GraphLiteralText");
    entry->add_theme_constant_override("minimum_character_width", 0);
    entry->set_expand_to_text_length_enabled(true);
    size_pin_editor(entry, minimum_width, maximum_width);
}

/// Check whether an input currently receives a wired value.
bool input_connected(const Array &connections, int64_t id, const StringName &pin) {
    for (const Variant &item : connections) {
        if (item.get_type() == Variant::DICTIONARY) {
            const Dictionary connection = item;
            if (static_cast<int64_t>(connection.get("to", 0)) == id && StringName(connection.get("to_pin", "")) == pin) {
                return true;
            }
        }
    }
    return false;
}

/// Read an explicit literal or the native signature's default.
Variant literal_value(const Dictionary &node, const Dictionary &pin) {
    const Dictionary values = node.get("values", Dictionary());
    return values.get(pin.get("id", StringName()), pin.get("default", Variant()));
}

/// Compare the graph operation independently of moves and edits to other input values.
Dictionary literal_owner(const Dictionary &node) {
    Dictionary owner = node.duplicate();
    owner.erase("position");
    owner.erase("values");
    return owner;
}

/// Determine whether the graph owner can supply an unconnected receiver.
bool accepts_owner(const Ref<VisualScript> &script, const StringName &expected) {
    return script.is_valid() && (expected.is_empty() ||
            ClassDBSingleton::get_singleton()->is_parent_class(script->get_graph_base(), expected));
}

/// A property can omit its unconnected Target when the script owner is a valid receiver.
bool has_implicit_property_target(const Ref<VisualScript> &script, const Dictionary &node, const Array &connections) {
    if (StringName(node.get("kind", StringName())) != StringName(get_property_kind)) {
        return false;
    }
    const Dictionary member = node.get("member", Dictionary());
    const StringName owner_class = member.get("class", StringName());
    const Dictionary values = node.get("values", Dictionary());
    return String(member.get("script_path", String())).is_empty() && !owner_class.is_empty() && accepts_owner(script, owner_class) &&
            !values.has(StringName("target")) && !values.has(String("target")) &&
            !input_connected(connections, node.get("id", 0), "target");
}

/// Count the editable components of common numeric vector and color values.
int32_t component_count(Variant::Type type) {
    switch (type) {
        case Variant::VECTOR2:
        case Variant::VECTOR2I: return 2;
        case Variant::VECTOR3:
        case Variant::VECTOR3I: return 3;
        case Variant::VECTOR4:
        case Variant::VECTOR4I:
        case Variant::COLOR: return 4;
        default: return 1;
    }
}

/// Identify integer scalar and vector types for exact numeric parsing.
bool integer_literal(Variant::Type type) {
    return type == Variant::INT || type == Variant::VECTOR2I ||
            type == Variant::VECTOR3I || type == Variant::VECTOR4I;
}

/// Construct the zero value of a supported vector type before editing a component.
Variant component_value(Variant::Type type, const Variant &existing) {
    if (existing.get_type() == type) {
        return existing;
    }
    switch (type) {
        case Variant::VECTOR2: return Vector2();
        case Variant::VECTOR2I: return Vector2i();
        case Variant::VECTOR3: return Vector3();
        case Variant::VECTOR3I: return Vector3i();
        case Variant::VECTOR4: return Vector4();
        case Variant::VECTOR4I: return Vector4i();
        case Variant::COLOR: return Color();
        default: return existing;
    }
}

/// Read one numeric component in the editor's display order.
String component_text(const Variant &value, int32_t component) {
    if (value.get_type() == Variant::NIL) {
        return {};
    }
    if (component < 0) {
        return String(value);
    }
    bool valid = false;
    bool out_of_bounds = false;
    const Variant field = value.get_indexed(component, valid, out_of_bounds);
    return valid && !out_of_bounds ? String(field) : String();
}

/// Create a non-actionable category while retaining its native disclosure affordance.
TreeItem *category(Tree *tree, TreeItem *parent, const String &name) {
    auto *item = tree->create_item(parent);
    item->set_text(0, name);
    item->set_selectable(0, false);
    item->set_custom_color(0, ui::color(ui::Surface::MUTED));
    item->set_text_overrun_behavior(0, TextServer::OVERRUN_TRIM_ELLIPSIS);
    return item;
}

/// Insert a selectable built-in graph action under its category.
TreeItem *action(Tree *tree, TreeItem *parent, const String &label, const StringName &kind, const String &help) {
    auto *item = tree->create_item(parent);
    item->set_text(0, label);
    Dictionary value;
    value["kind"] = kind;
    item->set_metadata(0, value);
    item->set_tooltip_text(0, help);
    item->set_text_overrun_behavior(0, TextServer::OVERRUN_TRIM_ELLIPSIS);
    item->set_icon_max_width(0, ui::pixels(tree, 12));
    const StringName glyph = node_role_icon(kind);
    if (!glyph.is_empty() && tree->has_theme_icon(glyph, "EditorIcons")) {
        item->set_icon(0, tree->get_theme_icon(glyph, "EditorIcons"));
        item->set_icon_modulate(0, node_accent(kind));
    }
    return item;
}

/// Match an action or type name using plain, case-insensitive search.
bool matches_query(const String &label, const String &query) {
    return query.is_empty() || label.findn(query) >= 0;
}

/// Describe a read-only Godot constant in both the picker and its graph card.
String global_constant_help(const GlobalConstant &constant) {
    const String scope = constant.enumeration ? String("@GlobalScope.") + constant.enumeration : String("@GlobalScope");
    return scope + String(".") + String(constant.name) + String(" = ") + String::num_int64(constant.value);
}

/// Explain which shared object this reference reads.
String singleton_help(const Dictionary &node) {
    const String name = node.get("singleton", String());
    const StringName source = node.get("source", StringName());
    if (source == StringName("autoload")) {
        return "Get the " + name + " Autoload from Project Settings.";
    }
    return "Get the " + name + " singleton registered with Godot.";
}

/// Offer downcasts and upcasts between related types, including refinements of script references.
bool possible_object_cast(const Dictionary &source, const Dictionary &target) {
    if (static_cast<int64_t>(source.get("type", Variant::NIL)) == Variant::NIL) return true;
    if (static_cast<int64_t>(source.get("type", Variant::NIL)) != Variant::OBJECT) return false;
    const StringName source_class = source.get("class", StringName("Object"));
    const StringName target_class = target.get("class", StringName("Object"));
    if (!ClassDB::is_parent_class(source_class, target_class) && !ClassDB::is_parent_class(target_class, source_class)) return false;
    const String source_script = source.get("script_path", String());
    const String target_script = target.get("script_path", String());
    return source_script.is_empty() || target_script.is_empty() || script_paths_compatible(source_script, target_script) ||
            script_paths_compatible(target_script, source_script);
}

/// List the project's authored Input Map action names. Godot's editor has a separate active InputMap.
PackedStringArray project_input_actions() {
    PackedStringArray names;
    auto *settings = ProjectSettings::get_singleton();
    for (const Dictionary &info : settings->get_property_list()) {
        const String name = info.get("name", String());
        if (name.begins_with("input/") && settings->has_setting(name)) {
            names.append(name.trim_prefix("input/"));
        }
    }
    names.sort();
    return names;
}

/// Prefer a literal search match while preserving the first matching category order.
void prefer_result(TreeItem *&first, TreeItem *candidate, const String &query) {
    if (!first || (!query.is_empty() && candidate->get_text(0).nocasecmp_to(query) == 0 && first->get_text(0).nocasecmp_to(query) != 0)) {
        first = candidate;
    }
}

/// Offer contextual insertion when a new node has a compatible pin.
StringName insertion_pin(const Dictionary &document, const Dictionary &graph, const Dictionary &node, bool output,
        const Dictionary &origin, const StringName &base_class, const Dictionary *presentation = nullptr) {
    for (const Dictionary &candidate : graph_pins(document, graph, node, output, base_class)) {
        if (is_execution(candidate) != is_execution(origin)) {
            continue;
        }
        if (is_execution(candidate)) {
            return candidate.get("id", StringName());
        }
        const bool compatible = output ? pins_compatible(candidate, origin) : pins_compatible(origin, candidate);
        const bool matches_type = !presentation || (output ? pins_compatible(candidate, *presentation) : pins_compatible(*presentation, candidate));
        if (compatible && matches_type) return candidate.get("id", StringName());
    }
    return {};
}

}

void register_visual_script_canvas() {
    GDREGISTER_INTERNAL_CLASS(VisualScriptCanvas);
    GDREGISTER_INTERNAL_CLASS(VisualScriptGraphNode);
    register_visual_script_asset_field();
    register_visual_script_tag_field();
    register_visual_script_panels();
}

VisualScriptWindow::~VisualScriptWindow() {
    if (history) {
        memdelete(history);
    }
}

void VisualScriptWindow::build(const Callable &reveal) {
    auto *editor = EditorInterface::get_singleton();
    set_name("CrimsonVisualScriptWorkspace");
    reveal_workspace = reveal;
    // Crimson owns these labels. Avoid partial translation using Godot's unrelated editor messages.
    set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
    set_theme(graph_theme(editor->get_editor_scale(), get_window()));

    history = memnew(UndoRedo);
    history->set_max_steps(64);

    auto *surface = ui::surface_panel(this, ui::Surface::CANVAS);
    surface->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    set_clip_contents(true);
    workbench = surface;
    empty_workspace = ui::surface_panel(this, ui::Surface::CANVAS);
    empty_workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    auto *prompt = ui::label(empty_workspace, "Open a Visual Script from FileSystem or the Scene dock.", ui::LabelStyle::MUTED);
    prompt->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    prompt->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    const double scale = get_theme_default_base_scale();
    const Color none(0, 0, 0, 0);
    auto *layout = add<VBoxContainer>(surface);
    layout->add_theme_constant_override("separation", 0);
    auto *top = add<PanelContainer>(layout);
    top->set_name("VisualScriptToolbar");
    top->set_custom_minimum_size(Vector2(0, ui::pixels(this, 42)));
    const auto strip = ui::box(graph_color(GraphTone::SIDEBAR), none, 12, 5, scale);
    strip->set_border_width(SIDE_BOTTOM, ui::pixels(this, 1));
    strip->set_border_color(graph_color(GraphTone::DIVIDER));
    top->add_theme_stylebox_override("panel", strip);
    auto *toolbar = add<HBoxContainer>(top);
    toolbar->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::md));
    auto *location = add<HBoxContainer>(toolbar);
    location->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    location->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::xs));
    event_root = ui::button(location, "Event Graph", "Return to the event graph.",
            callable_mp(this, &VisualScriptWindow::open_graph).bind(0), ui::ButtonStyle::QUIET);
    event_root->set_name("GraphEventRoot");
    event_root->set_theme_type_variation("GraphToolbarButton");
    event_root->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    event_root->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    event_root->add_theme_color_override("font_color", ui::color(ui::Surface::MUTED));
    // The active root is a heading. On another graph, this same position offers the return action.
    event_root->add_theme_color_override("font_disabled_color", graph_color(GraphTone::TEXT));
    graph_separator = add<TextureRect>(location);
    graph_separator->set_texture(ui::icon(this, "chevron_right"));
    graph_separator->set_custom_minimum_size(Vector2(ui::pixels(this, 12), ui::pixels(this, 12)));
    graph_separator->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    graph_separator->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    graph_separator->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    graph_caption = ui::label(location, {}, ui::LabelStyle::HEADING);
    graph_caption->set_name("VisualScriptActiveGraph");
    graph_caption->add_theme_font_size_override("font_size", ui::pixels(this, 13));
    graph_caption->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    graph_caption->set_clip_text(true);
    graph_caption->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    graph_caption->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    build_navigation_search(toolbar);
    auto *actions = add<HBoxContainer>(toolbar);
    actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    actions->set_alignment(BoxContainer::ALIGNMENT_END);

    // Insert the debugger strip immediately ahead of the work area when it is active.
    const int32_t debug_strip_index = layout->get_child_count();
    auto *frame_margin = ui::inset(layout, ui::spacing::md, ui::spacing::md);
    frame_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    auto *workspace = ui::split_pane(frame_margin, nullptr, nullptr);
    workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    workspace->set_dragger_visibility(SplitContainer::DRAGGER_HIDDEN);
    auto *graph_canvas = add<VisualScriptCanvas>(workspace);
    auto *right = ui::split_pane_vertical(workspace, nullptr, nullptr);
    right->set_name("VisualScriptRightSplit");
    right->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    right->set_stretch_ratio(0.26);
    canvas = graph_canvas;
    const Callable fit_panes = callable_mp_static(+[](Control *host, Control *graph, Control *sidebar) {
        const real_t width = std::max<real_t>(0, host->get_size().x - 2 * ui::pixels(host, ui::spacing::md));
        graph->set_custom_minimum_size(Vector2(std::min<real_t>(ui::pixels(host, 260), width * 0.45f), 0));
        sidebar->set_custom_minimum_size(Vector2(std::min<real_t>(ui::pixels(host, 240), width * 0.42f), 0));
    }).bind(this, canvas, right);
    connect("resized", fit_panes);
    fit_panes.call();
    custom_code = add<VisualScriptCustomCode>(this);
    custom_code->set_theme(get_theme());
    custom_code->build();
    custom_code->set_force_native(false);
    panels = add<VisualScriptPanels>(this);
    panels->set_collection_commit([this](const String &title, const Dictionary &document, const Callable &redo, const Callable &undo) {
        commit(title, document, true, false, redo, undo);
    });
    auto *symbols = add<VBoxContainer>(right);
    symbols->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    symbols->set_stretch_ratio(0.38);
    panels->build_left(symbols, [this](const String &title, const Dictionary &document) { commit(title, document); },
            [this](int64_t graph_id) { open_graph(graph_id); }, [this](const Dictionary &node) {
                insertion_position = ((canvas->get_size() / 2.0 + canvas->get_scroll_offset()) / canvas->get_zoom()).snapped(Vector2(16, 16));
                pending_pin = {};
                insert_node(node);
            }, [this](int64_t id, Control *trigger) { edit_custom_code(id, trigger); });
    auto *inspection = ui::split_pane_vertical(right, nullptr, nullptr);
    inspection->set_stretch_ratio(0.62);
    inspection->set_dragger_visibility(SplitContainer::DRAGGER_HIDDEN);
    auto *details = add<VBoxContainer>(inspection);
    details->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    details->set_custom_minimum_size(Vector2(0, ui::pixels(this, 104)));
    details->set_stretch_ratio(0.55);
    panels->build_right(details);
    build_debug_controls(layout, inspection);
    layout->move_child(debug_toolbar, debug_strip_index);
    graph_canvas->drop_symbols = [this](Vector2 point, const Array &options) { show_dropped_options(point, options); };
    graph_canvas->drop_file = [this](Vector2 point, const String &path) { drop_asset(point, path); };
    graph_canvas->can_drop_file = [this](Vector2 point) { return node_at_canvas_point(point) == nullptr; };
    graph_canvas->scene_dock_reference = [this](const Dictionary &drag) { return panels->scene_dock_reference(drag); };
    graph_canvas->preview_symbol_drop = [this](Vector2 point, const Array &options) { preview_variable_drop(point, options); };
    graph_canvas->pin_pointer = [this](std::optional<Vector2> point) { update_pin_hover(point); };
    graph_canvas->add_reroute = [this](Vector2 point) { reroute_wire(point); };
    graph_canvas->wire_input = [this](const Ref<InputEvent> &event) { return wire_tool_input(event); };
    graph_canvas->input_blocked = [this]() { return canvas_input_blocked(); };
    graph_canvas->early_wire_input = [this](const Ref<InputEvent> &event) { return early_wire_tool_input(event); };
    graph_canvas->wire_pan = [this](double delta) { tick_wire_pan(delta); };
    graph_canvas->cancel_pointer_gestures = [this]() { cancel_wire_gestures(); };
    graph_canvas->window_state_changed = [this]() { schedule_canvas_wire_refresh(); };
    graph_canvas->debugger_tooltip = [this](Vector2 point) { return debug_canvas_tooltip(point); };
    graph_canvas->build_interaction_overlay();
    graph_canvas->set_process(false);
    canvas->set_name("GraphCanvas");
    canvas->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    canvas->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    canvas->set_stretch_ratio(0.74);
    ui::style_graph_canvas(canvas);
    canvas->set_connection_lines_thickness(1.85);
    canvas->set_show_menu(false);
    canvas->set_minimap_enabled(false);
    canvas->set_right_disconnects(false);
    canvas->connect("visibility_changed", callable_mp(this, &VisualScriptWindow::canvas_visibility_changed));
    canvas->connect("popup_request", callable_mp(this, &VisualScriptWindow::show_palette));
    canvas->connect("connection_request", callable_mp(this, &VisualScriptWindow::connect_nodes));
    canvas->connect("disconnection_request", callable_mp(this, &VisualScriptWindow::disconnect_nodes));
    canvas->connect("connection_to_empty", callable_mp(this, &VisualScriptWindow::show_output_palette));
    canvas->connect("connection_from_empty", callable_mp(this, &VisualScriptWindow::show_input_palette));
    canvas->connect("connection_drag_started", callable_mp(this, &VisualScriptWindow::native_wire_started));
    canvas->connect("connection_drag_ended", callable_mp(this, &VisualScriptWindow::native_wire_ended));
    canvas->connect("begin_node_move", callable_mp(this, &VisualScriptWindow::native_move_started));
    canvas->connect("end_node_move", callable_mp(this, &VisualScriptWindow::finish_move), Object::CONNECT_DEFERRED);
    canvas->connect("delete_nodes_request", callable_mp(this, &VisualScriptWindow::delete_selected));
    canvas->connect("copy_nodes_request", callable_mp(this, &VisualScriptWindow::copy_selected_nodes));
    canvas->connect("cut_nodes_request", callable_mp(this, &VisualScriptWindow::cut_selected_nodes));
    canvas->connect("paste_nodes_request", callable_mp(this, &VisualScriptWindow::paste_nodes));
    canvas->connect("duplicate_nodes_request", callable_mp(this, &VisualScriptWindow::duplicate_selected_nodes).bind(false));
    canvas->connect("node_selected", callable_mp(this, &VisualScriptWindow::canvas_selection_changed));
    canvas->connect("node_deselected", callable_mp(this, &VisualScriptWindow::canvas_selection_changed));
    canvas->connect("graph_elements_linked_to_frame_request", callable_mp(this, &VisualScriptWindow::attach_comment_members));

    palette = add<PopupPanel>(this);
    palette->set_name("GraphNodePalette");
    palette->set_theme(get_theme());
    palette->connect("popup_hide", callable_mp(this, &VisualScriptWindow::palette_closed));
    auto *palette_pad = ui::inset(palette, ui::spacing::md, ui::spacing::md);
    auto *palette_column = add<VBoxContainer>(palette_pad);
    palette_column->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::sm));
    search = ui::popup_search_input(palette_column, "Search nodes or classes", "Filter nodes by name or Godot class.");
    search->set_name("GraphPaletteSearch");
    search->set_theme_type_variation("GraphPickerSearch");
    search->set_custom_minimum_size(Vector2(0, ui::pixels(this, 29)));
    results = add<Tree>(palette_column);
    results->set_name("GraphPaletteResults");
    results->set_theme_type_variation("GraphPickerResults");
    results->set_hide_root(true);
    results->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    results->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    results->set_allow_reselect(true);
    results->set_allow_search(false);
    results->set_h_scroll_enabled(false);
    results->set_v_scroll_enabled(true);
    results->set_select_mode(Tree::SELECT_ROW);
    results->add_theme_constant_override("draw_guides", 0);
    results->add_theme_constant_override("draw_relationship_lines", 0);
    for (const Variant &child : results->get_children(true)) {
        if (auto *scroll = Object::cast_to<Control>(child.get_validated_object()); scroll && scroll->is_class("VScrollBar")) {
            scroll->set_name("GraphPaletteScrollBar");
            scroll->set_theme_type_variation("GraphPickerScrollBar");
            scroll->set_accessibility_name("Scroll node results");
            break;
        }
    }
    graph_canvas->picker_wire = [this](Vector2 &from, Vector2 &to, Color &color) {
        return palette_wire_geometry(from, to, color);
    };
    canvas->connect("scroll_offset_changed", callable_mp(graph_canvas, &VisualScriptCanvas::refresh_picker_wire).unbind(1));
    canvas->connect("resized", callable_mp(graph_canvas, &VisualScriptCanvas::refresh_picker_wire));
    auto *palette_footer = add<HBoxContainer>(palette_column);
    context_sensitive = ui::checkbox(palette_footer, "Context Sensitive", ui::CheckState::CHECKED,
            callable_mp_static(+[](bool, VisualScriptWindow *window) {
                window->refresh_palette_members();
                window->filter_palette(window->search->get_text());
            }).bind(this));
    context_sensitive->set_name("GraphPaletteContextSensitive");
    context_sensitive->set_tooltip_text("Show only nodes that can connect to the dragged pin. Turn off to browse all nodes available in this graph.");
    auto *footer_space = add<Control>(palette_footer);
    footer_space->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    add_button = ui::button(palette_footer, "Add Node", "Insert the selected graph node.",
            callable_mp(this, &VisualScriptWindow::insert_selected), ui::ButtonStyle::PRIMARY);
    add_button->set_theme_type_variation("GraphSaveButton");
    add_button->set_custom_minimum_size(Vector2(ui::pixels(this, 66), ui::pixels(this, 24)));
    add_button->set_name("InsertGraphNode");
    search->connect("text_changed", callable_mp(this, &VisualScriptWindow::filter_palette));
    search->connect("text_submitted", callable_mp(this, &VisualScriptWindow::submit_search));
    search->connect("gui_input", callable_mp(this, &VisualScriptWindow::palette_search_input));
    results->connect("item_selected", callable_mp(this, &VisualScriptWindow::palette_selection_changed));
    results->connect("cell_selected", callable_mp(this, &VisualScriptWindow::palette_selection_changed));
    results->connect("item_mouse_selected", callable_mp(this, &VisualScriptWindow::palette_row_clicked));
    results->connect("item_activated", callable_mp(this, &VisualScriptWindow::insert_selected));
    build_graph_menus(actions);

    problem_dialog = ui::editor_modal(this, surface, callable_mp(static_cast<Control *>(canvas), &Control::grab_focus).bind(false));
    problem_dialog->set_name("VisualScriptProblem");
    problem_dialog->set_cancel_visible(false);
    switch_dialog = ui::editor_modal(this, surface, callable_mp(this, &VisualScriptWindow::save_and_switch),
            callable_mp(this, &VisualScriptWindow::cancel_switch));
    switch_dialog->set_name("VisualScriptSaveBeforeOpen");
    switch_dialog->set_confirm_text("Save and Open");
    switch_dialog->set_alternate_action("Discard Edits", callable_mp(this, &VisualScriptWindow::discard_and_switch));
    reload_dialog = ui::editor_modal(this, surface, callable_mp(this, &VisualScriptWindow::reload_graph));
    reload_dialog->set_name("VisualScriptReload");
    reload_dialog->set_confirm_text("Discard and Reload");
    reload_dialog->set_destructive(true);
    variable_edit_dialog = ui::editor_modal(this, surface, callable_mp(this, &VisualScriptWindow::confirm_variable_edit),
            callable_mp(this, &VisualScriptWindow::clear_variable_edit));
    variable_edit_dialog->set_name("VisualScriptChangeVariableType");
    variable_edit_dialog->set_validator(callable_mp(this, &VisualScriptWindow::validate_variable_edit));
    document_paste_dialog = ui::editor_modal(this, surface, callable_mp(this, &VisualScriptWindow::confirm_document_paste),
            callable_mp(this, &VisualScriptWindow::clear_document_paste));
    document_paste_dialog->set_name("VisualScriptPasteConflicts");
    document_paste_dialog->set_confirm_text("Paste");
    document_paste_dialog->set_validator(callable_mp(this, &VisualScriptWindow::validate_document_paste));
    panels->set_variable_edit_confirmation([this](VisualScriptPanels::VariableEdit edit, const Dictionary &next,
                                                   const String &explanation, const Callable &finalize) {
        if (active_modal() || script.is_null()) return;
        prepare_modal();
        variable_edit_owner = script;
        variable_edit_before = script->get_graph();
        variable_edit_next = next;
        variable_edit_finalize = finalize;
        String prompt;
        String confirm;
        switch (edit) {
            case VisualScriptPanels::VariableEdit::TYPE:
                prompt = "Change variable type?";
                confirm = "Change Type";
                variable_edit_title = "Change Variable Type";
                break;
            case VisualScriptPanels::VariableEdit::RENAME_EXPORTED:
                prompt = "Rename exported variable?";
                confirm = "Rename Variable";
                variable_edit_title = "Rename Declaration";
                break;
            case VisualScriptPanels::VariableEdit::HIDE_EXPORTED:
                prompt = "Hide variable from Inspector?";
                confirm = "Hide Variable";
                variable_edit_title = "Hide Variable from Inspector";
                break;
            case VisualScriptPanels::VariableEdit::CUT_EXPORTED:
                prompt = "Cut exported variable?";
                confirm = "Cut Variable";
                variable_edit_title = "Cut Declaration";
                break;
            case VisualScriptPanels::VariableEdit::DELETE_EXPORTED:
                prompt = "Delete exported variable?";
                confirm = "Delete Variable";
                variable_edit_title = "Delete Declaration";
                break;
        }
        variable_edit_dialog->set_destructive(edit != VisualScriptPanels::VariableEdit::TYPE);
        variable_edit_dialog->set_confirm_text(confirm);
        variable_edit_dialog->open(prompt, explanation);
    });
    refresh_commands();
    workbench->hide();
}

void VisualScriptWindow::open(const Ref<VisualScript> &value) {
    if (value.is_null() || active_modal()) {
        return;
    }
    if (reveal_workspace.is_valid()) {
        reveal_workspace.call();
    }
    if (custom_code->has_draft()) {
        enter_workspace();
        return;
    }
    if (script == value) {
        canvas->grab_focus();
        return;
    }
    if (autosave_timer.is_valid()) {
        save_graph(false);
    }
    if (!unsaved_status().is_empty()) {
        next_script = value;
        prepare_modal();
        switch_dialog->open("Save Visual Script?", "Save changes to " + script->get_path().get_file() + " before opening another graph?");
        return;
    }
    use_script(value);
}

void VisualScriptWindow::use_script(const Ref<VisualScript> &value) {
    cancel_autosave();
    autosave_error = String();
    custom_code->dismiss();
    if (script.is_valid()) {
        script->disconnect("changed", callable_mp(this, &VisualScriptWindow::graph_changed));
    }
    script = value;
    empty_workspace->hide();
    workbench->show();
    active_graph = 0;
    reset_wire_tools();
    history->clear_history();
    canvas->set_zoom(1.0);
    canvas->set_scroll_offset(Vector2());
    reset_navigation();
    saved_graph = script->get_graph();
    saved_disk_hash = "";
    const String path = script->get_path();
    if (!path.is_empty()) {
        const String disk_hash = FileAccess::get_sha256(path);
        const Ref<VisualScript> disk = ResourceLoader::get_singleton()->load(path, "VisualScript", ResourceLoader::CACHE_MODE_IGNORE);
        if (!disk_hash.is_empty() && disk.is_valid() && FileAccess::get_sha256(path) == disk_hash) {
            script->set_graph_base(disk->get_graph_base());
            script->set_graph(disk->get_graph());
            saved_graph = script->get_graph();
            saved_disk_hash = disk_hash;
        } else {
            show_problem("The saved script could not be read. Use Reload before saving.");
        }
    }
    script->connect("changed", callable_mp(this, &VisualScriptWindow::graph_changed));
    debug_watch_erase_missing(path, saved_graph);
    rebuild();
    debug_apply_snapshot(debug_snapshot);
    canvas->grab_focus();
}

void VisualScriptWindow::enter_workspace() {
    if (workbench && is_visible_in_tree()) custom_code->resume();
}

void VisualScriptWindow::leave_workspace() {
    if (!workbench) return;
    panels->close_popups();
    if (auto *modal = active_modal()) {
        modal->cancel();
    }
    custom_code->suspend();
    close_drop_choice();
    cancel_wire_gestures();
    if (palette->is_visible()) {
        palette->hide();
    }
    if (context_menu->is_visible()) context_menu->hide();
    close_navigation_search();
    if (graph_tools_menu->is_visible()) graph_tools_menu->hide();
    if (alignment_menu->is_visible()) alignment_menu->hide();
}

void VisualScriptWindow::edit_custom_code(int64_t node_id, Control *trigger) {
    if (script.is_null() || custom_code->is_visible()) {
        return;
    }
    const Dictionary document = script->get_graph();
    const int64_t node_graph = active_graph;
    const Dictionary body = graph_body(document, node_graph);
    const Dictionary node = find_graph_record(graph_array(body, "nodes"), node_id);
    if (StringName(node.get("kind", StringName())) != StringName(custom_script_kind)) {
        return;
    }
    cancel_wire_gestures();
    const uint64_t script_id = script->get_instance_id();
    const StringName base = script->get_graph_base();
    const String original_code = node.get("code", String());
    custom_code->edit(original_code,
            [document, body, node, base](const String &source) {
                Dictionary draft = node.duplicate();
                draft["code"] = source;
                return build_custom_script_source(document, body, draft, base, CustomScriptSourcePurpose::ANALYSIS);
            },
            [this, script_id, node_graph, node_id, node, base](const String &source) -> String {
                const String stale = "This node changed while its code was open. Copy your draft, cancel, and open the node again.";
                if (script.is_null() || script->get_instance_id() != script_id || active_graph != node_graph ||
                        script->get_graph_base() != base) {
                    return stale;
                }
                const Dictionary latest = script->get_graph();
                Dictionary current_body = graph_body(latest, node_graph).duplicate();
                Array nodes = graph_array(current_body, "nodes").duplicate();
                for (int64_t index = 0; index < nodes.size(); ++index) {
                    if (nodes[index].get_type() != Variant::DICTIONARY) {
                        continue;
                    }
                    const Dictionary current_node = nodes[index];
                    if (static_cast<int64_t>(current_node.get("id", 0)) != node_id) {
                        continue;
                    }
                    if (current_node != node) {
                        return stale;
                    }
                    Dictionary draft = current_node.duplicate();
                    draft["code"] = source;
                    String error;
                    const Dictionary edited = reconcile_custom_script_pins(draft, error);
                    if (!error.is_empty()) {
                        return error;
                    }
                    if (edited == current_node) {
                        return {};
                    }
                    nodes[index] = edited;
                    current_body["nodes"] = nodes;
                    const Dictionary next = replace_graph_body(latest, node_graph, current_body);
                    if (visual_script_edit_breaks_wires(latest, next, base)) {
                        return "Disconnect the affected wires before changing these source pins.";
                    }
                    commit("Edit Custom Script Code", next);
                    return {};
                }
                return stale;
            }, trigger, canvas);
}

String VisualScriptWindow::unsaved_status() const {
    if (script.is_null() || script->get_graph() == saved_graph) {
        return {};
    }
    return "Save changes to " + script->get_path().get_file() + "?";
}

void VisualScriptWindow::save_external_data() {
    if (!unsaved_status().is_empty()) {
        save_graph();
    }
}

bool VisualScriptWindow::save_before_run() {
    if (save_graph(false)) return true;
    if (reveal_workspace.is_valid()) reveal_workspace.call();
    show_problem(autosave_error);
    return false;
}

void VisualScriptWindow::save_and_switch() {
    const Ref<VisualScript> pending = next_script;
    next_script.unref();
    save_graph();
    if (pending.is_valid() && unsaved_status().is_empty()) {
        use_script(pending);
    }
}

void VisualScriptWindow::discard_and_switch() {
    const Ref<VisualScript> pending = next_script;
    next_script.unref();
    if (pending.is_valid()) {
        script->set_graph(saved_graph);
        use_script(pending);
    }
}

void VisualScriptWindow::cancel_switch() {
    next_script.unref();
}

void VisualScriptWindow::schedule_autosave() {
    if (unsaved_status().is_empty() || !autosave_error.is_empty()) {
        cancel_autosave();
        return;
    }
    if (autosave_timer.is_valid()) {
        autosave_timer->set_time_left(autosave_delay);
    } else {
        autosave_timer = get_tree()->create_timer(autosave_delay, true, false, true);
        autosave_timer->connect("timeout", callable_mp(this, &VisualScriptWindow::autosave_graph), CONNECT_ONE_SHOT);
    }
}

void VisualScriptWindow::cancel_autosave() {
    if (autosave_timer.is_null()) return;
    autosave_timer->disconnect("timeout", callable_mp(this, &VisualScriptWindow::autosave_graph));
    autosave_timer.unref();
}

void VisualScriptWindow::autosave_graph() {
    autosave_timer.unref();
    if (active_modal()) {
        schedule_autosave();
    } else {
        save_graph(false);
    }
}

bool VisualScriptWindow::save_graph(bool interactive) {
    cancel_autosave();
    if (unsaved_status().is_empty()) return true;
    const String path = script->get_path();
    const auto failed = [this, interactive](const String &message) {
        const bool first_failure = autosave_error.is_empty();
        autosave_error = message;
        refresh_graph_tools();
        if (interactive) {
            if (reveal_workspace.is_valid()) reveal_workspace.call();
            show_problem(message);
        } else if (first_failure) {
            UtilityFunctions::push_warning("[Crimson] Visual Script autosave paused. " + message);
        }
        return false;
    };
    if (path.is_empty()) {
        return failed("Save the attached script as a .crimsonscript file first.");
    }
    if (saved_disk_hash.is_empty() || FileAccess::get_sha256(path) != saved_disk_hash) {
        return failed("The script changed on disk. Use Graph Tools > Reload from Disk to read the current file.");
    }
    const Error result = ResourceSaver::get_singleton()->save(script, path);
    if (result != OK) {
        return failed("Could not save " + path.get_file() + " (error " + String::num_int64(result) + "). Press Ctrl+S to retry.");
    }
    autosave_error = String();
    saved_graph = script->get_graph();
    saved_disk_hash = FileAccess::get_sha256(path);
    EditorInterface::get_singleton()->get_resource_filesystem()->update_file(path);
    refresh_graph_tools();
    refresh_commands();
    return true;
}

void VisualScriptWindow::request_reload() {
    if (script.is_null() || script->get_path().is_empty() || active_modal()) {
        return;
    }
    if (unsaved_status().is_empty()) {
        reload_graph();
        return;
    }
    prepare_modal();
    reload_dialog->open("Reload Visual Script?", "Reloading discards unsaved changes to this graph.");
}

void VisualScriptWindow::reload_graph() {
    const String path = script->get_path();
    const String disk_hash = FileAccess::get_sha256(path);
    const Ref<VisualScript> disk = ResourceLoader::get_singleton()->load(path, "VisualScript", ResourceLoader::CACHE_MODE_IGNORE);
    if (disk_hash.is_empty() || disk.is_null() || FileAccess::get_sha256(path) != disk_hash) {
        show_problem("Could not reload " + path.get_file() + ". The current graph is still open.");
        return;
    }
    panels->discard_collection_drafts();
    cancel_autosave();
    autosave_error = String();
    script->set_graph_base(disk->get_graph_base());
    script->set_graph(disk->get_graph());
    saved_graph = script->get_graph();
    saved_disk_hash = disk_hash;
    history->clear_history();
    debug_watch_erase_missing(path, saved_graph);
    debug_apply_snapshot(debug_snapshot);
    refresh_graph_tools();
    refresh_commands();
    canvas->grab_focus();
}

void VisualScriptWindow::undo_graph() {
    if (script.is_valid() && history->has_undo()) {
        history->undo();
        schedule_autosave();
        refresh_commands();
    }
}

void VisualScriptWindow::redo_graph() {
    if (script.is_valid() && history->has_redo()) {
        history->redo();
        schedule_autosave();
        refresh_commands();
    }
}

void VisualScriptWindow::shortcut(const Ref<InputEvent> &event) {
    const Ref<InputEventKey> key = event;
    if (script.is_null() || key.is_null()) {
        return;
    }
    const bool authoring_dialog = canvas_input_blocked();
    const auto *focused = get_viewport()->gui_get_focus_owner();
    if (key->get_keycode() == KEY_SPACE && focused == canvas && !authoring_dialog) {
        // Space belongs to the graph picker. Keep repeats and releases out of the native pan shortcut.
        if (key->is_pressed() && !key->is_echo() && !native_node_drag && !key->is_alt_pressed() &&
                !key->is_shift_pressed() && !key->is_command_or_control_pressed()) {
            show_center_palette();
        }
        get_viewport()->set_input_as_handled();
        return;
    }
    if (native_node_drag || authoring_dialog || !key->is_pressed() || key->is_echo()) {
        return;
    }
    if (debug_shortcut(key) || navigation_shortcut(key) || (focused == canvas && graph_tool_shortcut(key))) {
        get_viewport()->set_input_as_handled();
        return;
    }
    if (!key->is_command_or_control_pressed()) {
        if (focused == canvas && !key->is_alt_pressed() && !key->is_shift_pressed()) {
            if (key->get_keycode() == KEY_Q) {
                straighten_connections();
                get_viewport()->set_input_as_handled();
            } else if (key->get_keycode() == KEY_F9) {
                const PackedInt64Array selection = selected_nodes();
                const bool any_set = std::any_of(selection.begin(), selection.end(),
                        [this](int64_t id) { return debug_has_breakpoint(id); });
                set_selection_breakpoints(!any_set);
                get_viewport()->set_input_as_handled();
            }
        }
        return;
    }
    if (key->get_keycode() == KEY_S) {
        save_graph();
        get_viewport()->set_input_as_handled();
    } else if (!Object::cast_to<LineEdit>(focused)) {
        if (key->get_keycode() == KEY_Y || (key->get_keycode() == KEY_Z && key->is_shift_pressed())) {
            redo_graph();
            get_viewport()->set_input_as_handled();
        } else if (key->get_keycode() == KEY_Z) {
            undo_graph();
            get_viewport()->set_input_as_handled();
        }
    }
}

void VisualScriptWindow::_input(const Ref<InputEvent> &event) {
    if (!is_visible_in_tree()) return;
    if (dismiss_navigation_input(event)) {
        get_viewport()->set_input_as_handled();
        return;
    }
    auto *focused = get_viewport()->gui_get_focus_owner();
    if (focused && is_ancestor_of(focused)) shortcut(event);
}

bool VisualScriptWindow::canvas_input_blocked() const {
    return drop_choice || active_modal() || (palette && palette->is_visible()) ||
            (navigation_search && navigation_search->is_visible()) || (custom_code && custom_code->is_visible());
}

void VisualScriptWindow::refresh_commands() {
    const bool in_subgraph = script.is_valid() && active_graph != 0;
    event_root->set_disabled(!in_subgraph);
    event_root->set_default_cursor_shape(in_subgraph ? Control::CURSOR_POINTING_HAND : Control::CURSOR_ARROW);
    String root_help = in_subgraph ? String("Return to the event graph.") : String("Event Graph");
    if (script.is_valid()) {
        const String path = script->get_path();
        if (!path.is_empty()) root_help += "\n" + path;
        root_help += "\nGodot base class: " + String(script->get_graph_base());
    }
    event_root->set_tooltip_text(root_help);
    graph_separator->set_visible(in_subgraph);
    graph_caption->set_visible(in_subgraph);
    if (in_subgraph) {
        const String title = graph_title(script->get_graph(), active_graph);
        graph_caption->set_text(title);
        graph_caption->set_tooltip_text(title);
    }
}

Dictionary VisualScriptWindow::current_graph(const Dictionary &document) const {
    return graph_body(document, active_graph);
}

/// Compare the constrained lanes independently of an explicit legacy Variant lane.
static Dictionary constrained_dictionary_lanes(const Dictionary &node) {
    Dictionary result;
    const Variant saved = node.get("dictionary_types", Variant());
    if (saved.get_type() != Variant::DICTIONARY) return result;
    const Dictionary lanes = saved;
    for (const char *side : {"key", "value"}) {
        const Variant entry = lanes.get(side, Variant());
        if (entry.get_type() == Variant::DICTIONARY && int64_t(Dictionary(entry).get("type", Variant::NIL)) != Variant::NIL) {
            result[side] = entry;
        }
    }
    return result;
}

/// Detect existing wires lost specifically because an existing dictionary changed its output type.
static bool dictionary_type_breaks_wires(const Dictionary &document, const Dictionary &before, const Dictionary &graph,
        const Array &normalized_wires, const StringName &base) {
    const Array previous_nodes = graph_array(before, "nodes");
    const Array nodes = graph_array(graph, "nodes");
    Array with_previous_types;
    for (int64_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = nodes[index];
        if (String(node.get("kind", String())) != "make_dictionary" || !node.has("dictionary_types")) continue;
        const Dictionary previous = find_graph_record(previous_nodes, node.get("id", 0));
        if (String(previous.get("kind", String())) != "make_dictionary" ||
                constrained_dictionary_lanes(node) == constrained_dictionary_lanes(previous)) continue;
        if (with_previous_types.is_empty()) with_previous_types = nodes.duplicate();
        Dictionary restored = node.duplicate();
        if (previous.has("dictionary_types")) restored["dictionary_types"] = previous["dictionary_types"];
        else restored.erase("dictionary_types");
        with_previous_types[index] = restored;
    }
    if (with_previous_types.is_empty()) return false;

    Dictionary unchanged_types = graph.duplicate();
    unchanged_types["nodes"] = with_previous_types;
    const Array previous_wires = graph_array(before, "connections");
    const Array unaffected_wires = graph_array(prune_incompatible_loop_wires(document, unchanged_types, base), "connections");
    for (const Variant &wire : unaffected_wires) {
        if (!normalized_wires.has(wire) && previous_wires.has(wire)) return true;
    }
    return false;
}

bool VisualScriptWindow::commit_graph(const String &title, const Dictionary &graph, bool redraw, bool merge) {
    const Dictionary document = script->get_graph();
    const StringName base = script->get_graph_base();
    const Dictionary normalized = prune_incompatible_loop_wires(document, graph, base);
    const Array normalized_wires = graph_array(normalized, "connections");
    if (normalized_wires.size() < graph_array(graph, "connections").size() &&
            dictionary_type_breaks_wires(document, current_graph(document), graph, normalized_wires, base)) {
        show_problem("Disconnect this dictionary's incompatible output wire before choosing a new pin type.");
        return false;
    }
    const Dictionary next = replace_graph_body(document, active_graph, normalized);
    if (next.is_empty()) return false;
    commit(title, next, redraw, merge);
    return true;
}

void VisualScriptWindow::open_graph(int64_t id) {
    if (script.is_null() || id == active_graph) return;
    const Dictionary document = script->get_graph();
    if (graph_body(document, id).is_empty()) {
        return;
    }
    record_location();
    const float zoom = canvas->get_zoom();
    restore_location({id, canvas->get_size() * 0.5f / zoom, zoom, {}});
    if (!find_graph_record(graph_array(document, "functions"), id).is_empty()) {
        get_tree()->connect("process_frame", callable_mp(this, &VisualScriptWindow::frame_opened_function).bind(id, camera_request),
                Object::CONNECT_ONE_SHOT | Object::CONNECT_DEFERRED);
    }
}

void VisualScriptWindow::canvas_selection_changed(Node *) {
    if (script.is_null() || rebuilding_canvas) {
        return;
    }
    const PackedInt64Array selected = selected_nodes();
    panels->select_graph_node(selected.size() == 1
                    ? find_graph_record(graph_array(current_graph(script->get_graph()), "nodes"), selected[0]) : Dictionary());
}

ui::EditorModal *VisualScriptWindow::active_modal() const {
    for (auto *modal : {problem_dialog, switch_dialog, reload_dialog, variable_edit_dialog, document_paste_dialog}) {
        if (modal && modal->is_open()) {
            return modal;
        }
    }
    return nullptr;
}

void VisualScriptWindow::prepare_modal() {
    panels->close_popups();
    close_drop_choice();
    cancel_wire_gestures();
    close_navigation_search();
    for (Window *popup : {static_cast<Window *>(palette),
                 static_cast<Window *>(context_menu), static_cast<Window *>(alignment_menu), static_cast<Window *>(graph_tools_menu)}) {
        if (popup && popup->is_visible()) {
            popup->hide();
        }
    }
}

void VisualScriptWindow::show_problem(const String &message) {
    if (!active_modal()) {
        prepare_modal();
        problem_dialog->open("Visual Script", message);
    }
}

bool VisualScriptWindow::validate_variable_edit() {
    if (script.is_valid() && script == variable_edit_owner && script->get_graph() == variable_edit_before) {
        return true;
    }
    variable_edit_dialog->set_validation_message("The graph changed while this prompt was open. Cancel and try the edit again.");
    return false;
}

void VisualScriptWindow::confirm_variable_edit() {
    const Dictionary next = variable_edit_next;
    const String title = variable_edit_title;
    const Callable finalize = variable_edit_finalize;
    clear_variable_edit();
    if (finalize.is_valid()) {
        finalize.call();
    }
    commit(title, next);
}

void VisualScriptWindow::clear_variable_edit() {
    variable_edit_owner.unref();
    variable_edit_before = Dictionary();
    variable_edit_next = Dictionary();
    variable_edit_title = String();
    variable_edit_finalize = Callable();
}

void VisualScriptWindow::commit(const String &title, const Dictionary &next, bool redraw, bool merge,
        const Callable &redo_view, const Callable &undo_view) {
    if (script.is_null()) {
        return;
    }
    const Dictionary before = script->get_graph();
    if (next == before && !redo_view.is_valid()) {
        return;
    }
    applying_edit = !redraw;
    history->create_action(title, merge ? UndoRedo::MERGE_ENDS : UndoRedo::MERGE_DISABLE);
    history->add_do_method(callable_mp(script.ptr(), &VisualScript::set_graph).bind(next));
    history->add_undo_method(callable_mp(script.ptr(), &VisualScript::set_graph).bind(before));
    if (redo_view.is_valid() && undo_view.is_valid()) {
        history->add_do_method(redo_view);
        history->add_undo_method(undo_view);
    }
    history->commit_action();
    applying_edit = false;
    schedule_autosave();
    refresh_commands();
}

void VisualScriptWindow::graph_changed() {
    if (pending_document_paste) pending_document_paste->stale = true;
    debug_prune_breakpoints();
    refresh_commands();
    const Dictionary document = script->get_graph();
    debug_watch_erase_missing(script->get_path(), document);
    if (debug_pane->is_visible()) {
        debug_apply_snapshot(debug_snapshot);
    }
    panels->update_document(document);
    if (!applying_edit && !rebuild_scheduled) {
        rebuild_scheduled = true;
        callable_mp(this, &VisualScriptWindow::rebuild_when_idle).call_deferred();
    }
}

void VisualScriptWindow::rebuild_when_idle() {
    // Removing a GraphNode under pointer capture makes Godot drop the pending
    // mouse release. Wait until the native drag has finished before rebuilding.
    if (rebuild_scheduled && !native_node_drag) {
        rebuild();
    }
}

bool VisualScriptWindow::canvas_ports_available() const {
    if (!canvas->is_visible_in_tree()) {
        return false;
    }
    // The stock headless display backend reports its dummy root as minimized.
    if (DisplayServer::get_singleton()->get_name() == "headless") {
        return true;
    }
    const auto *host = get_window();
    return host && host->get_mode() != Window::MODE_MINIMIZED;
}

void VisualScriptWindow::canvas_visibility_changed() {
    if (!canvas->is_visible_in_tree()) {
        update_pin_hover({});
        cancel_wire_gestures();
        canvas->clear_connections();
        canvas_wires_suspended = true;
        return;
    }
    for (const auto &[id, widget] : widgets) {
        widget->queue_sort();
    }
    schedule_canvas_wire_refresh();
}

void VisualScriptWindow::schedule_canvas_wire_refresh() {
    if (!canvas_wire_refresh_queued) {
        canvas_wire_refresh_queued = true;
        // A visibility notification propagates down the canvas before Godot sorts the port rows.
        // Reconnect after that layout pass, including when reopening the same script.
        get_tree()->connect("process_frame", callable_mp(this, &VisualScriptWindow::refresh_canvas_wires),
                Object::CONNECT_ONE_SHOT | Object::CONNECT_DEFERRED);
    }
}

void VisualScriptWindow::refresh_canvas_wires() {
    canvas_wire_refresh_queued = false;
    if (!canvas_ports_available()) {
        if (!canvas_wires_suspended) {
            canvas->clear_connections();
            canvas_wires_suspended = true;
        }
        return;
    }
    if (script.is_valid() && canvas_wires_suspended && !rebuild_scheduled) {
        canvas_wires_suspended = false;
        attach_canvas_wires(graph_array(current_graph(script->get_graph()), "connections"));
        debug_refresh_pin_labels();
        update_wire_highlights();
    }
}

void VisualScriptWindow::rebuild() {
    rebuild_scheduled = false;
    // GraphEdit defers native wire draws and would otherwise read ports from replacement widgets.
    cancel_native_wire();
    static_cast<VisualScriptCanvas *>(canvas)->clear_selection_gesture(false);
    rebuilding_canvas = true;
    update_pin_hover({});
    node_wire_preview.reset();
    rearm_node_move = false;
    canvas->clear_connections();
    std::set<int64_t> selected;
    for (const auto &[id, widget] : widgets) {
        if (widget->is_selected()) {
            selected.insert(id);
        }
        canvas->remove_child(widget);
        widget->queue_free();
    }
    widgets.clear();
    for (const auto &[id, frame] : comment_frames) {
        canvas->remove_child(frame);
        frame->queue_free();
    }
    comment_frames.clear();
    if (script.is_null()) {
        rebuilding_canvas = false;
        return;
    }
    const Dictionary document = script->get_graph();
    if (current_graph(document).is_empty()) {
        active_graph = 0;
        selected.clear();
    }
    const Dictionary graph = current_graph(document);
    const Array nodes = graph_array(graph, "nodes");
    const Array connections = graph_array(graph, "connections");
    const StringName base = script->get_graph_base();
    const auto pin_presentations = graph_pin_presentation(document, graph, base);
    for (const Variant &item : nodes) {
        if (item.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary node = item;
        const int64_t id = node.get("id", 0);
        if (id <= 0 || widgets.find(id) != widgets.end()) {
            continue;
        }
        auto *widget = add<VisualScriptGraphNode>(canvas);
        widget->set_notify_local_transform(true);
        widget->set_name(widget_name(id));
        widget->set_meta(node_id_key, id);
        widget->connect("gui_input", callable_mp(this, &VisualScriptWindow::navigation_node_input).bind(id));
        widget->set_position_offset(node.get("position", Vector2()));
        widget->connect("position_offset_changed", callable_mp(this, &VisualScriptWindow::recover_node_move_start).bind(widget));
        const StringName kind = node.get("kind", "");
        const bool dot = kind == StringName("reroute");
        const bool getter = kind == StringName("get_variable");
        const bool self = kind == StringName("self");
        const bool collection_constructor = kind == StringName("make_array") || kind == StringName("make_dictionary");
        const bool sequence = kind == StringName("sequence");
        String variable_name;
        const bool variable_setter = kind == StringName("set_variable");
        const bool scene_reference = kind == StringName("scene_node");
        const bool singleton_reference = kind == StringName("singleton");
        const bool property_getter = kind == StringName(get_property_kind);
        const bool property_setter = kind == StringName(set_property_kind);
        const bool variable = getter || variable_setter;
        const auto presentation = pin_presentations.find(id);
        const bool has_presentation = presentation != pin_presentations.end();
        const Array inputs = has_presentation ? presentation->second.contract_inputs : graph_pins(document, graph, node, false, base);
        const Array outputs = has_presentation ? presentation->second.contract_outputs : graph_pins(document, graph, node, true, base);
        const Array shown_inputs = has_presentation ? presentation->second.inputs : inputs;
        const Array shown_outputs = has_presentation ? presentation->second.outputs : outputs;
        const bool implicit_property_target = property_getter && has_implicit_property_target(script, node, connections);
        const bool self_property = implicit_property_target && !outputs.is_empty() && !bool(node.get("show_target", false));
        const bool compact_read = getter || self_property || self || scene_reference || singleton_reference;
        std::optional<Color> value_ink;
        if ((self || scene_reference || singleton_reference) && !shown_outputs.is_empty()) {
            value_ink = pin_appearance(shown_outputs[0]).ink;
            widget->set_variable_border(*value_ink);
        } else if (variable || property_getter || property_setter) {
            for (const Dictionary &pin : (getter || property_getter) ? shown_outputs : shown_inputs) {
                if (!is_execution(pin) && StringName(pin.get("id", StringName())) == StringName("value")) {
                    value_ink = pin_appearance(pin).ink;
                    break;
                }
            }
            if ((variable || self_property) && value_ink) {
                widget->set_variable_border(*value_ink);
            }
        }
        using ui::GraphNodeStyle;
        const GraphNodeStyle style = dot ? GraphNodeStyle::REROUTE : compact_read ? GraphNodeStyle::COMPACT_VALUE :
                variable_setter ? GraphNodeStyle::VARIABLE_SETTER : GraphNodeStyle::STANDARD;
        ui::style_graph_node(widget, style, node_header(kind, node), value_ink);
        if (dot) {
            widget->set_meta(routing_dot_key, true);
            widget->set_mouse_filter(Control::MOUSE_FILTER_PASS);
            widget->set_default_cursor_shape(Control::CURSOR_MOVE);
        }
        if (!dot) {
            widget->style_card(compact_read);
            if (!compact_read && !collection_constructor) {
                ui::style_graph_node_title_icon(widget, value_ink ? StringName("MemberProperty") : node_role_icon(kind),
                        value_ink.value_or(node_accent(kind)));
            }
        }
        if (kind == StringName(get_property_kind) || kind == StringName(set_property_kind) || kind == StringName(call_method_kind)) {
            draw_member(widget, kind, node);
        } else {
            const Dictionary declaration = card_declaration(document, graph, node, String(kind));
            String title = declared_name(declaration, String(kind));
            if (getter) {
                variable_name = declaration.get("name", String("Missing variable"));
            }
            if (!declaration.is_empty()) {
                widget->set_tooltip_text(declaration.get("description", String()));
            }
            if (kind == StringName(ready_kind) || kind == StringName("lifecycle")) {
                title = "On Ready";
                const StringName callback = node.get("callback", StringName("_ready"));
                widget->set_tooltip_text(visual_script_method_help("Node", callback));
                for (const Dictionary &event : lifecycle_events()) {
                    if (StringName(event.get("callback", StringName())) == callback) {
                        title = event.get("title", title);
                        break;
                    }
                }
            } else if (kind == StringName(create_kind)) {
                title = "Create " + String(node.get("class", String()));
                widget->set_tooltip_text("Construct a scene node without parenting it.");
            } else if (kind == StringName("native_signal")) {
                const StringName signal = node.get("signal", StringName());
                const StringName script_name = node.get("script_name", StringName());
                title = "On " + String(signal).capitalize();
                widget->set_tooltip_text(script_name.is_empty() ? visual_script_signal_help(node.get("class", StringName()), signal) :
                        String(script_name) + "." + String(signal));
            } else if (kind == StringName("operator")) {
                title = String(node.get("operator", String())).capitalize();
            } else if (kind == StringName("create_script_instance")) {
                title = "Create " + String(node.get("script_name", StringName("Script Instance")));
            } else if (kind == StringName("make_value") || kind == StringName("break_value")) {
                const auto value_type = visual_value_type(node);
                const bool assemble = kind == StringName("make_value");
                const String operation = assemble ? "Make " : "Break ";
                title = operation + (value_type == Variant::VARIANT_MAX ? String("Unsupported Value") : Variant::get_type_name(value_type));
                widget->set_tooltip_text(assemble ? "Build a value from its typed fields." : "Read the individual fields of a value.");
            } else if (kind == StringName("await_signal") && node.has("signal")) {
                title = "Await " + String(node.get("signal", StringName())).capitalize();
            } else if (kind == StringName("input_poll") || kind == StringName("cast")) {
                title = node.get("label", title);
            } else if (kind == StringName("literal") && node.has("global_constant")) {
                const StringName symbol = node.get("global_constant", StringName());
                title = String(symbol);
                if (const GlobalConstant *constant = find_global_constant(symbol)) {
                    widget->set_tooltip_text(global_constant_help(*constant));
                }
            } else if (kind == StringName(custom_script_kind)) {
                title = custom_script_title(node);
                widget->set_tooltip_text(title + "\nRun GDScript with typed inputs and outputs.");
                widget->set_accessibility_description("Custom Script. Runs GDScript with typed inputs and outputs.");
                widget->show_custom_script_badge();
                if (auto *label = Object::cast_to<Label>(widget->get_titlebar_hbox()->get_child(0))) {
                    label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
                }
            } else if (kind == StringName("switch_enum")) {
                const StringName enum_type = node.get("enum_type", StringName());
                const String authored_name = authored_enum_label(document, enum_type);
                title = "Switch on " + (authored_name.is_empty() ? String(enum_type).get_slice(".", 1).capitalize() : authored_name);
            }
            widget->set_title(dot ? String() : title.is_empty() ? String(kind).capitalize() : title);
        }
        if (dot) {
            auto *titlebar = widget->get_titlebar_hbox();
            if (auto *label = Object::cast_to<Label>(titlebar->get_child(0))) {
                label->hide();
            }
            titlebar->hide();
        }
        if (dot) {
            if (!inputs.is_empty() && !outputs.is_empty()) {
                add_ports(widget, document, inputs[0], outputs[0]);
                widget->set_tooltip_text("Drag the dot to move it. Drag just beside it to attach a wire.");
            }
        } else if (compact_read) {
            const Dictionary output = outputs.is_empty() ? Dictionary() : Dictionary(outputs[0]);
            const Dictionary member = node.get("member", Dictionary());
            String caption = self_property ? String(member.get("name", StringName())).capitalize() : variable_name;
            StringName caption_node = self_property ? StringName("GraphPropertyName") : StringName("GraphVariableName");
            if (scene_reference) {
                caption = node.get("label", String());
                if (caption.is_empty()) {
                    const NodePath source = node.get("source", NodePath());
                    caption = source.get_name_count() > 0 ? String(source.get_name(source.get_name_count() - 1)) : String("Self");
                }
                caption_node = "GraphSceneReferenceName";
                widget->set_title(caption);
                widget->set_tooltip_text(caption + String("\n") + reference_type_label(output, document));
            } else if (self) {
                caption = "Self";
                caption_node = "GraphSelfName";
                widget->set_title(caption);
                const String scene_name = node.get("label", String());
                const String tooltip_heading = scene_name.is_empty() ? caption : caption + String(" (") + scene_name + String(")");
                widget->set_tooltip_text(tooltip_heading + String("\n") + reference_type_label(output, document));
            } else if (singleton_reference) {
                caption = node.get("singleton", String("Singleton"));
                caption_node = "GraphSingletonName";
                widget->set_title(caption);
                widget->set_tooltip_text(singleton_help(node) + String("\n") + reference_type_label(output, document));
            }
            add_ports(widget, document, {}, output, {}, caption, caption_node);
            if (self_property) {
                widget->set_accessibility_description("Reads this property on Self. Show Target from the context menu to wire another object.");
            }
        } else {
            const int64_t count = std::max(inputs.size(), outputs.size());
            for (int64_t index = 0; index < count; ++index) {
                const Dictionary input = index < inputs.size() ? Dictionary(inputs[index]) : Dictionary();
                const Dictionary output = index < outputs.size() ? Dictionary(outputs[index]) : Dictionary();
                const StringName pin = input.get("id", StringName());
                Dictionary display = input;
                String help;
                const bool source_owned = input.get("source_owned", false);
                if (source_owned) {
                    help = "Connect a value or use Edit Code to set the unwired value.";
                } else if (pin == StringName("target") && static_cast<int64_t>(input.get("type", -1)) == Variant::OBJECT) {
                    const bool owner = property_getter ? implicit_property_target : String(input.get("script_path", String())).is_empty() &&
                            accepts_owner(script, input.get("class", StringName()));
                    if (owner && (property_getter || !input_connected(connections, id, pin))) {
                        display = input.duplicate();
                        display["label"] = "Target (Self)";
                    }
                    help = owner ? "An unconnected Target uses the node running this script." : "Connect a compatible object.";
                } else if (kind == StringName(add_child_kind) && pin == StringName("parent")) {
                    help = "When unconnected, the graph owner is the parent.";
                }
                add_ports(widget, document, display, output, help, {}, {}, [&](HBoxContainer *row) {
                    if (source_owned || input.is_empty() || is_execution(input) || input_connected(connections, id, pin)) {
                        return;
                    }
                    if (kind == StringName(print_kind) && pin == StringName("message")) {
                        auto *message = ui::text_input(row, node.get("message", String()),
                                "String. Text to print in Godot's Output panel.", ui::FieldState::EDITABLE, "Message");
                        message->set_name("PrintMessage");
                        message->set_accessibility_name("Print message");
                        style_pin_text(message, 68, 160);
                        message->connect("text_changed", callable_mp(this, &VisualScriptWindow::set_message).bind(id));
                    } else {
                        add_literal(row, id, node, input, document);
                    }
                });
            }
        }
        if (kind == StringName(custom_script_kind)) {
            auto *edit = ui::button(widget, "Edit Code", "Open this node's GDScript editor.", {}, ui::ButtonStyle::QUIET);
            edit->set_name("VisualScriptOpenCustomCode");
            ui::style_graph_inline_control(edit, "GraphNodeAction");
            edit->connect("pressed", callable_mp(this, &VisualScriptWindow::edit_custom_code).bind(id, edit));
        }
        present_node_pins(widget, document, has_presentation ? presentation->second : GraphPinLayout{inputs, outputs, {}, {}},
                collection_constructor);
        if (collection_constructor || sequence) {
            const bool dictionary = kind == StringName("make_dictionary");
            const int64_t arity = sequence ? outputs.size() : int64_t(node.get("collection_size", 1));
            auto *actions_row = add<HBoxContainer>(widget);
            actions_row->set_alignment(BoxContainer::ALIGNMENT_END);
            actions_row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            auto *add_pin = ui::button(actions_row, "Add Pin",
                    sequence ? "Add an execution output at the end of the sequence." :
                            dictionary ? "Add a key and value." : "Add another array input.", {}, ui::ButtonStyle::QUIET);
            add_pin->set_name(sequence ? "SequenceAddPin" : "CollectionAddPin");
            ui::style_graph_inline_control(add_pin, "GraphNodeAction");
            add_pin->set_button_icon(ui::icon(add_pin, "add_circle"));
            add_pin->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
            add_pin->add_theme_constant_override("icon_max_width", ui::pixels(add_pin, 13));
            add_pin->set_disabled(arity >= (sequence ? sequence_output_limit : visual_script_max_collection_size));
            add_pin->connect("pressed", (sequence ? callable_mp(this, &VisualScriptWindow::edit_sequence_pins) :
                    callable_mp(this, &VisualScriptWindow::edit_collection_constructor)).bind(id, StringName()),
                    Object::CONNECT_DEFERRED);
        }
        if (kind == StringName("preload_resource")) {
            const Dictionary pin = preload_asset_pin(node);
            if (!pin.is_empty()) {
                auto *row = add<HBoxContainer>(widget);
                row->add_theme_constant_override("separation", ui::pixels(row, ui::spacing::xs));
                add_literal(row, id, node, pin, document, true);
                add_preload_preview(widget, literal_value(node, pin), node.get("asset_class", StringName("Resource")));
            }
        }
        if (node.get("disabled", false)) {
            ui::style_graph_node_disabled(widget);
        }
        widgets.emplace(id, widget);
        widget->set_selected(pending_canvas_selection.is_empty() ? selected.find(id) != selected.end() :
                pending_canvas_selection.has(id));
    }
    if (!canvas_ports_available()) {
        canvas_wires_suspended = true;
    } else if (canvas_wires_suspended) {
        schedule_canvas_wire_refresh();
    } else {
        attach_canvas_wires(connections);
    }
    refresh_debug_markers();
    debug_refresh_pin_labels();
    pending_canvas_selection.clear();
    rebuild_graph_comments(graph);
    for (PendingPin *pin : {&multi_connect_source, &exchange_source}) {
        if (pin->node <= 0) continue;
        const auto found = widgets.find(pin->node);
        if (found == widgets.end() || pin_index(found->second, pin->output, pin->pin) < 0 ||
                pin_descriptor(found->second, pin->output, pin_index(found->second, pin->output, pin->pin)) != pin->descriptor) {
            *pin = {};
        }
    }
    if (multi_connect_source.node <= 0) {
        batch_connecting = false;
        batch_connection_targets.clear();
        auto *graph_canvas = static_cast<VisualScriptCanvas *>(canvas);
        graph_canvas->wire_preview.clear();
        graph_canvas->queue_redraw();
    }
    update_wire_highlights();
    panels->refresh(document, active_graph, script);
    rebuilding_canvas = false;
    refresh_commands();
    if (navigation_search->is_visible()) {
        refresh_navigation_search(navigation_query->get_text());
    }
}

void VisualScriptWindow::attach_canvas_wires(const Array &connections) {
    for (const Variant &item : connections) {
        if (item.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary connection = item;
        const auto from = widgets.find(static_cast<int64_t>(connection.get("from", 0)));
        const auto to = widgets.find(static_cast<int64_t>(connection.get("to", 0)));
        if (from == widgets.end() || to == widgets.end()) {
            continue;
        }
        const auto source = pin_index(from->second, true, connection.get("from_pin", ""));
        const auto destination = pin_index(to->second, false, connection.get("to_pin", ""));
        if (source >= 0 && destination >= 0 &&
                canvas->connect_node(from->second->get_name(), source, to->second->get_name(), destination) == OK) {
            static_cast<VisualScriptGraphNode *>(from->second)->mark_connected(source, true);
            static_cast<VisualScriptGraphNode *>(to->second)->mark_connected(destination, false);
        }
    }
}

void VisualScriptWindow::refresh_debug_markers() {
    for (const auto &[id, widget] : widgets) {
        static_cast<VisualScriptGraphNode *>(widget)->set_debug_state(debug_has_breakpoint(id), debug_is_current(id));
    }
}

void VisualScriptWindow::draw_member(GraphNode *widget, const StringName &kind, const Dictionary &node) {
    const Dictionary member = node.get("member", Dictionary());
    widget->set_title(member_title(kind, member));
    const String problem = validate_native_member(kind, member);
    widget->set_tooltip_text(problem.is_empty() ? member_help(kind, member) : problem);
    if (!problem.is_empty()) {
        auto *warning = ui::label(widget, "Member unavailable", ui::LabelStyle::MUTED);
        warning->add_theme_font_size_override("font_size", ui::pixels(widget, ui::graph_pin_font_size));
        warning->set_tooltip_text(problem);
        warning->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    }
}

void VisualScriptWindow::add_literal(HBoxContainer *row, int64_t id, const Dictionary &node,
        const Dictionary &pin, const Dictionary &document, bool preload) {
    const StringName pin_id = pin.get("id", StringName());
    if (pin_id.is_empty()) {
        return;
    }
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", Variant::NIL)));
    const StringName class_name = pin.get("class", StringName());
    const String field_name = String("Literal_") + String(pin_id);
    const String label = pin.get("label", String());
    const Variant value = literal_value(node, pin);
    const String help = label + String(". ") + type_label(pin) + String(" literal used when the pin has no wire.");
    const auto tag_kind = tag_field_kind(pin, node);
    if (!preload && tag_kind != TagFieldKind::NONE) {
        const Ref<VisualScript> owner_script = script;
        const int64_t owner_graph = active_graph;
        const Dictionary owner_node = literal_owner(node);
        auto *field = tag_field(row, tag_kind, value,
                [this, owner_script, owner_graph, owner_node, id, pin, pin_id](const Variant &next, const Variant &previous) {
                    if (script != owner_script || active_graph != owner_graph) return false;
                    const Dictionary latest_document = script->get_graph();
                    const Dictionary latest_graph = current_graph(latest_document);
                    const Dictionary latest_node = find_graph_record(graph_array(latest_graph, "nodes"), id);
                    if (literal_owner(latest_node) != owner_node) return false;
                    for (const Dictionary &current_pin : graph_pins(latest_document, latest_graph, latest_node, false,
                            script->get_graph_base())) {
                        if (current_pin == pin) {
                            return literal_value(latest_node, current_pin) == previous && set_literal(next, id, pin_id, false);
                        }
                    }
                    return false;
                });
        field->set_name(field_name);
        field->set_accessibility_description(help);
        ui::style_graph_inline_control(field, "GraphLiteralSelect");
        size_pin_editor(field, 110, 168);
        return;
    }
    if (pin.get("input_action", false)) {
        Array values = Array::make(StringName());
        PackedStringArray labels;
        labels.append("Select action");
        int selected = 0;
        for (const String &action_name : project_input_actions()) {
            if (String(value) == action_name) selected = labels.size();
            labels.append(action_name);
            values.append(StringName(action_name));
        }
        const String unknown = value;
        if (selected == 0 && !unknown.is_empty()) {
            selected = labels.size();
            labels.append(unknown + String(" (missing)"));
        }
        auto *field = ui::select(row, labels, selected,
                callable_mp(this, &VisualScriptWindow::set_enum_literal).bind(values, id, pin_id));
        field->set_name(field_name);
        auto *trigger = field->get_trigger();
        trigger->set_accessibility_name(label);
        trigger->set_tooltip_text("Choose a Godot Input Map action in Project Settings. Connect a StringName to choose the action dynamically.");
        trigger->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        ui::style_graph_inline_control(trigger, "GraphLiteralSelect");
        size_pin_editor(field, 104, 160);
        if (selected >= values.size()) field->set_item_disabled(selected, true);
        return;
    }
    if (type == Variant::INT && pin.has("enum_type")) {
        const StringName enumeration = pin.get("enum_type", StringName());
        Array options = authored_enum_entries(document, enumeration);
        if (options.is_empty()) {
            options = native_enum_entries(enumeration);
        }
        if (!options.is_empty()) {
            PackedStringArray labels;
            Array values;
            int32_t selected = -1;
            for (const Dictionary &entry : options) {
                const Variant option = entry["value"];
                if (option == value && selected < 0) {
                    selected = labels.size();
                }
                labels.append(entry.get("name", entry.get("label", String())));
                values.append(option);
            }
            const bool unnamed = selected < 0;
            if (unnamed) {
                selected = labels.size();
                labels.append(value.get_type() == Variant::NIL ? String("Select a value") :
                        "Unknown value: " + String::num_int64(static_cast<int64_t>(value)));
            }
            auto *field = ui::select(row, labels, selected,
                    callable_mp(this, &VisualScriptWindow::set_enum_literal).bind(values, id, pin_id));
            field->set_name(field_name);
            auto *trigger = field->get_trigger();
            trigger->set_accessibility_name(label);
            trigger->set_tooltip_text(help);
            trigger->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
            ui::style_graph_inline_control(trigger, "GraphLiteralSelect");
            const Ref<Font> font = trigger->get_theme_font("font");
            const int width = font.is_valid() ? std::ceil(font->get_string_size(labels[selected], HORIZONTAL_ALIGNMENT_LEFT, -1,
                    trigger->get_theme_font_size("font_size")).x) + ui::pixels(field, 25) : ui::pixels(field, 72);
            size_pin_editor(field, 72, 136);
            field->set_custom_minimum_size(Vector2(std::clamp(width, ui::pixels(field, 72), ui::pixels(field, 136)),
                    ui::pixels(field, ui::graph_literal_height)));
            field->set_item_disabled(selected, unnamed);
            return;
        }
    }
    if (type == Variant::OBJECT && !class_name.is_empty() && ClassDBSingleton::get_singleton()->is_parent_class(class_name, "Resource")) {
        const Ref<VisualScript> owner_script = script;
        const int64_t owner_graph = active_graph;
        auto *picker = asset_field(row, class_name, help + String(" Drop a saved project resource here."),
                value, [this, owner_script, owner_graph, id, pin](const Ref<Resource> &asset) {
                    return script == owner_script && active_graph == owner_graph && set_resource_literal(asset, id, pin);
                }, true, preload);
        picker->set_name(field_name);
        picker->set_accessibility_name(label);
        if (preload) {
            picker->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        } else {
            size_pin_editor(picker, 113, 136);
        }
        return;
    }
    if (type == Variant::BOOL) {
        const bool unset = node.has("dictionary_types") && value.get_type() == Variant::NIL;
        const auto state = unset ? ui::CheckState::MIXED : static_cast<bool>(value) ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED;
        auto *check = ui::checkbox(row, {}, state,
                callable_mp(this, &VisualScriptWindow::set_bool_literal).bind(id, pin_id));
        ui::style_graph_inline_control(check);
        check->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
        check->add_theme_constant_override("check_v_offset", 0);
        check->set_name(field_name);
        check->set_tooltip_text(help);
        check->set_accessibility_name(label);
        if (unset) check->set_accessibility_description("No Boolean value set.");
        return;
    }
    const int32_t components = component_count(type);
    const bool scalar = type == Variant::STRING || type == Variant::STRING_NAME || type == Variant::NODE_PATH ||
            type == Variant::INT || type == Variant::FLOAT || type == Variant::NIL;
    if (!scalar && components == 1) {
        return;
    }
    auto *fields = add<HBoxContainer>(row);
    fields->set_name(field_name);
    fields->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    fields->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    fields->add_theme_constant_override("separation", ui::pixels(fields, ui::spacing::xs));
    const bool numeric = type == Variant::INT || type == Variant::FLOAT;
    for (int32_t index = 0; index < components; ++index) {
        const int32_t component = components == 1 ? -1 : index;
        const char *axis = (type == Variant::COLOR ? "RGBA" : "XYZW") + index;
        const String placeholder = component >= 0 ? String::chr(static_cast<char32_t>(*axis)) : numeric ? String() : label;
        auto *entry = ui::text_input(fields, component_text(value, component), help, ui::FieldState::EDITABLE, placeholder);
        style_pin_text(entry, component >= 0 ? 34 : numeric ? 30 : 64, component >= 0 ? 56 : numeric ? 104 : 160);
        entry->set_accessibility_name(component >= 0 ? label + String(" ") + placeholder : label);
        entry->connect("text_changed", callable_mp(this, &VisualScriptWindow::set_text_literal).bind(id, pin, component, entry));
    }
}

void VisualScriptWindow::edit_collection_constructor(int64_t id, const StringName &remove_pin) {
    if (script.is_null()) return;
    const Dictionary document = script->get_graph();
    Dictionary graph = current_graph(document);
    if (resize_collection_constructor(graph, id, remove_pin, document, script->get_graph_base())) {
        commit_graph(remove_pin.is_empty() ? "Add Collection Pin" : "Remove Collection Pin", graph);
    }
}

void VisualScriptWindow::edit_sequence_pins(int64_t id, const StringName &remove_pin) {
    if (script.is_null()) return;
    const Dictionary document = script->get_graph();
    Dictionary graph = current_graph(document).duplicate();
    Array nodes = graph_array(graph, "nodes").duplicate();
    for (int64_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].get_type() != Variant::DICTIONARY) continue;
        Dictionary node = nodes[index];
        if (int64_t(node.get("id", 0)) != id || StringName(node.get("kind", StringName())) != StringName("sequence")) continue;
        const Array outputs = graph_pins(document, graph, node, true, script->get_graph_base());
        const bool removing = !remove_pin.is_empty();
        const int64_t count = outputs.size();
        if ((removing && count <= 1) || (!removing && count >= sequence_output_limit)) return;

        if (removing) {
            Dictionary remap;
            for (int64_t output = 0; output < count; ++output) {
                const StringName pin = Dictionary(outputs[output])["id"];
                if (pin == remove_pin) {
                    remap[pin] = StringName();
                } else if (!remap.is_empty()) {
                    remap[pin] = Dictionary(outputs[output - 1])["id"];
                }
            }
            if (remap.is_empty()) return;
            Array wires;
            for (const Variant &entry : graph_array(graph, "connections")) {
                if (entry.get_type() == Variant::DICTIONARY) {
                    const Dictionary wire = entry;
                    const StringName source = wire.get("from_pin", StringName());
                    if (int64_t(wire.get("from", 0)) == id && remap.has(source)) {
                        const StringName successor = remap[source];
                        if (!successor.is_empty()) {
                            Dictionary shifted = wire.duplicate();
                            shifted["from_pin"] = successor;
                            wires.append(shifted);
                        }
                        continue;
                    }
                }
                wires.append(entry);
            }
            graph["connections"] = wires;
        }
        node = node.duplicate();
        node["count"] = count + (removing ? -1 : 1);
        nodes[index] = node;
        graph["nodes"] = nodes;
        commit_graph(removing ? "Remove Sequence Pin" : "Add Sequence Pin", graph);
        return;
    }
}

bool VisualScriptWindow::set_literal(const Variant &value, int64_t id, const StringName &pin, bool merge) {
    if (script.is_null()) {
        return false;
    }
    Dictionary next = current_graph(script->get_graph());
    if (input_connected(graph_array(next, "connections"), id, pin)) {
        return false;
    }
    Array nodes = graph_array(next, "nodes");
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        if (static_cast<int64_t>(node.get("id", 0)) != id) {
            continue;
        }
        const Variant prior_lanes = node.get("dictionary_types", Variant());
        if (!set_dictionary_constructor_literal_type(node, pin, value)) {
            show_problem("A dictionary input must use the type shown on its pin.");
            return false;
        }
        const bool lane_changed = node.get("dictionary_types", Variant()) != prior_lanes;
        Dictionary values = node.get("values", Dictionary());
        const bool shape_changed = values.get(pin, Variant()).get_type() != value.get_type();
        values[pin] = value;
        node["values"] = values;
        nodes[index] = node;
        next["nodes"] = nodes;
        if (!commit_graph(String("Edit Graph Pin ") + String::num_int64(id) + String(" ") + String(pin), next, false, merge)) {
            return false;
        }
        if (shape_changed || lane_changed) {
            refresh_pin_presentations();
        }
        return true;
    }
    return false;
}

void VisualScriptWindow::refresh_pin_presentations() {
    const Dictionary document = script->get_graph();
    const Dictionary graph = current_graph(document);
    const StringName base = script->get_graph_base();
    const auto pin_presentations = graph_pin_presentation(document, graph, base);
    for (const Variant &record : graph_array(graph, "nodes")) {
        if (record.get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = record;
        const int64_t id = node.get("id", 0);
        const auto widget = widgets.find(id);
        const auto presentation = pin_presentations.find(id);
        if (widget == widgets.end() || presentation == pin_presentations.end()) continue;
        const StringName kind = node.get("kind", StringName());
        const bool constructor = kind == StringName("make_array") || kind == StringName("make_dictionary");
        present_node_pins(static_cast<VisualScriptGraphNode *>(widget->second), document, presentation->second, constructor, true);
    }
    if (debug_pane->is_visible()) debug_refresh_pin_labels();
}

void VisualScriptWindow::set_bool_literal(bool value, int64_t id, const StringName &pin) {
    set_literal(value, id, pin);
}

void VisualScriptWindow::set_enum_literal(int64_t index, const Array &values, int64_t id, const StringName &pin) {
    if (index >= 0 && index < values.size()) {
        set_literal(values[index], id, pin);
    }
}

void VisualScriptWindow::set_text_literal(const String &text, int64_t id, const Dictionary &pin, int32_t component, LineEdit *field) {
    if (script.is_null()) {
        return;
    }
    const StringName name = pin.get("id", StringName());
    const auto apply_value = [&](const Variant &value) {
        if (set_literal(value, id, name)) return;
        const Dictionary graph = current_graph(script->get_graph());
        const Dictionary node = find_graph_record(graph_array(graph, "nodes"), id);
        field->set_text(component_text(literal_value(node, pin), component));
    };
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", Variant::NIL)));
    Variant value;
    if (type == Variant::STRING || type == Variant::NIL) {
        value = text;
    } else if (type == Variant::STRING_NAME) {
        value = StringName(text);
    } else if (type == Variant::NODE_PATH) {
        value = NodePath(text);
    } else {
        const String number = text.strip_edges();
        const bool integer = integer_literal(type);
        if (number.is_empty()) {
            return;
        }
        if (integer ? !number.is_valid_int() : !number.is_valid_float()) {
            return;
        }
        value = integer ? Variant(number.to_int()) : Variant(number.to_float());
        if (component >= 0) {
            const Array nodes = graph_array(current_graph(script->get_graph()), "nodes");
            for (const Variant &item : nodes) {
                const Dictionary node = item;
                if (static_cast<int64_t>(node.get("id", 0)) == id) {
                    Variant vector = component_value(type, literal_value(node, pin));
                    bool valid = false;
                    bool out_of_bounds = false;
                    vector.set_indexed(component, value, valid, out_of_bounds);
                    if (valid && !out_of_bounds) {
                        apply_value(vector);
                    }
                    return;
                }
            }
            return;
        }
    }
    if (pin_accepts_value(pin, value)) {
        apply_value(value);
    }
}

bool VisualScriptWindow::set_resource_literal(const Ref<Resource> &resource, int64_t id, const Dictionary &pin) {
    const StringName name = pin.get("id", StringName());
    const auto saved_resource = [&](bool preload, Variant &stored) {
        if (resource.is_null()) return true;
        const Dictionary reference = preload ? make_preload_reference(resource) : make_asset_reference(resource);
        if (reference.is_empty() || !pin_accepts_value(pin, resource)) {
            show_problem("Choose a valid saved project resource of type " + type_label(pin) + String("."));
            return false;
        }
        stored = reference;
        return true;
    };
    Variant stored;
    if (name != StringName("asset") || script.is_null()) {
        if (!saved_resource(false, stored)) return false;
        return set_literal(stored, id, name);
    }
    const Dictionary document = script->get_graph();
    Dictionary graph = current_graph(document);
    Array nodes = graph_array(graph, "nodes");
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        if (static_cast<int64_t>(node.get("id", 0)) != id) continue;
        if (StringName(node.get("kind", StringName())) != StringName("preload_resource")) break;
        if (!saved_resource(true, stored)) return false;
        Dictionary values = node.get("values", Dictionary());
        values[name] = stored;
        node["values"] = values;
        if (resource.is_valid()) {
            node["asset_class"] = resource->get_class();
        } else {
            node.erase("asset_class");
        }
        nodes[index] = node;
        graph["nodes"] = nodes;
        const StringName base = script->get_graph_base();
        for (const Dictionary &wire : graph_array(graph, "connections")) {
            if (static_cast<int64_t>(wire.get("from", 0)) != id) continue;
            if (connect_graph_pins(graph, id, wire.get("from_pin", StringName()), wire.get("to", 0),
                    wire.get("to_pin", StringName()), false, document, base).is_empty()) {
                show_problem("Disconnect the incompatible resource wires before changing this asset.");
                return false;
            }
        }
        return commit_graph("Change Preloaded Asset", graph);
    }
    if (!saved_resource(false, stored)) return false;
    return set_literal(stored, id, name);
}

void VisualScriptWindow::collect_native_classes() {
    if (!native_classes.empty()) {
        return;
    }
    const auto *classes = ClassDBSingleton::get_singleton();
    PackedStringArray names = classes->get_inheriters_from_class("Node");
    names.push_back("Node");
    names.sort();
    for (const String &name : names) {
        if (classes->class_get_api_type(name) == ClassDBSingleton::API_CORE) {
            native_classes[name].constructible = classes->can_instantiate(name);
        }
    }
    for (const String &name : names) {
        if (native_classes.find(name) != native_classes.end()) {
            const auto parent = native_classes.find(classes->get_parent_class(name));
            if (parent != native_classes.end()) {
                parent->second.children.push_back(name);
            }
        }
    }
}

void VisualScriptWindow::show_palette(Vector2 point) {
    inserting_on_wire = false;
    pending_pin = {};
    hovered_wire.clear();
    open_palette(point);
}

bool VisualScriptWindow::palette_filters_pin() const {
    return !pending_pin.pin.is_empty() && (inserting_on_wire || context_sensitive->is_pressed());
}

void VisualScriptWindow::refresh_palette_members() {
    member_options.clear();
    receiver_member_count = 0;
    const bool free = pending_pin.pin.is_empty();
    const bool execution = is_execution(pending_pin.picker_descriptor());
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pending_pin.picker_descriptor().get("type", Variant::NIL)));
    const StringName class_name = (free || execution || !pending_pin.output) ? script->get_graph_base() : type == Variant::OBJECT ?
            StringName(pending_pin.picker_descriptor().get("class", StringName())) : StringName();
    const auto add_event_choices = [&](const Array &signals) {
        member_options.append_array(signals);
        if (!free) {
            for (const Dictionary &signal : signals) {
                Dictionary wait = signal.duplicate();
                wait["kind"] = "await_signal";
                member_options.append(wait);
            }
        }
    };
    if (!class_name.is_empty()) {
        member_options = find_native_members(class_name);
        if (active_graph == 0 && (free || (pending_pin.output && !execution && type == Variant::OBJECT))) {
            add_event_choices(find_native_signals(class_name));
        }
        if (!free && pending_pin.output && type == Variant::OBJECT) {
            const String script_path = pending_pin.picker_descriptor().get("script_path", String());
            if (!script_path.is_empty()) {
                member_options.append_array(find_script_members(script_path));
                if (active_graph == 0) {
                    add_event_choices(find_script_signals(script_path));
                }
            }
        }
    }
    if (!free && !execution && pending_pin.output && type > Variant::NIL && type < Variant::VARIANT_MAX && type != Variant::OBJECT) {
        member_options = find_builtin_members(type);
    }
    if (!free && !execution && pending_pin.output) {
        receiver_member_count = member_options.size();
        if (!palette_filters_pin() && class_name != script->get_graph_base()) {
            member_options.append_array(find_native_members(script->get_graph_base()));
            if (active_graph == 0) {
                member_options.append_array(find_native_signals(script->get_graph_base()));
            }
        }
    }
    if (!palette_filters_pin() || execution || !pending_pin.output) {
        if (catalog_options.is_empty()) {
            for (int32_t index = Variant::BOOL; index < Variant::VARIANT_MAX; ++index) {
                if (index != Variant::OBJECT) {
                    catalog_options.append_array(find_builtin_members(static_cast<Variant::Type>(index)));
                }
            }
            catalog_options.append_array(find_utility_members());
            PackedStringArray native_owners = ClassDB::get_class_list();
            native_owners.sort();
            for (const String &owner : native_owners) {
                catalog_options.append_array(find_native_static_members(owner));
                if (!gameplay_tag_scope(owner).is_empty()) {
                    for (const Dictionary &entry : find_native_members(owner)) {
                        const Dictionary member = entry.get("member", Dictionary());
                        if (StringName(member.get("class", StringName())) == StringName(owner)) {
                            catalog_options.append(entry);
                        }
                    }
                }
            }
        }
        member_options.append_array(catalog_options);
    }
}

void VisualScriptWindow::open_palette(Vector2 point) {
    if (script.is_null()) {
        return;
    }
    update_pin_hover({});
    pending_pin.presentation.reset();
    if (const auto owner = widgets.find(pending_pin.node); owner != widgets.end()) {
        const int32_t port = pin_index(owner->second, pending_pin.output, pending_pin.pin);
        const Dictionary display = presented_pin_descriptor(owner->second, pending_pin.output, port);
        if (!display.is_empty() && display != pending_pin.descriptor) pending_pin.presentation = display;
    }
    collect_native_classes();
    context_sensitive->set_visible(!pending_pin.pin.is_empty());
    context_sensitive->set_disabled(inserting_on_wire);
    singleton_options = inserting_on_wire ? Array() : find_visual_script_singletons();
    script_cast_types = global_script_type_pins();
    if (native_cast_types.is_empty()) {
        for (const String &class_name : ClassDB::get_class_list()) {
            const auto api = ClassDBSingleton::get_singleton()->class_get_api_type(class_name);
            if (api == ClassDBSingleton::API_CORE || api == ClassDBSingleton::API_EXTENSION) {
                native_cast_types.append(class_name);
            }
        }
        native_cast_types.sort();
    }
    refresh_palette_members();
    insertion_position = ((point + canvas->get_scroll_offset()) / canvas->get_zoom()).snapped(Vector2(16, 16));
    search->clear();
    filter_palette("");
    const Transform2D canvas_to_workspace = get_global_transform_with_canvas().affine_inverse() *
            canvas->get_global_transform_with_canvas();
    const Vector2 local = canvas_to_workspace.xform(point);
    const Vector2i available(get_size());
    const Vector2i size = palette_extent();
    Vector2 corner = local;
    Vector2 source;
    if (!pending_pin.pin.is_empty() && canvas_pin_position(pending_pin.node, pending_pin.pin, pending_pin.output, source)) {
        const Vector2 source_local = canvas_to_workspace.xform(source);
        const float gap = ui::pixels(this, ui::spacing::lg);
        const Vector2 limit = Vector2(available - size).max(Vector2());
        Vector2 preferred = corner;
        preferred.x += local.x >= source_local.x ? gap : -size.x - gap;
        corner = preferred.clamp(Vector2(), limit);
        if (Rect2(corner, Vector2(size)).grow(gap).has_point(source_local)) {
            // Favor a nearby edge of the drop while keeping the originating socket visible.
            float best = std::numeric_limits<float>::infinity();
            for (const Vector2 candidate : {Vector2(source_local.x + gap, local.y), Vector2(source_local.x - gap - size.x, local.y),
                         Vector2(local.x, source_local.y + gap), Vector2(local.x, source_local.y - gap - size.y)}) {
                const Vector2 fitted = candidate.clamp(Vector2(), limit);
                const float distance = fitted.distance_squared_to(preferred);
                if (!Rect2(fitted, Vector2(size)).has_point(source_local) && distance < best) {
                    corner = fitted;
                    best = distance;
                }
            }
        }
    }
    corner = corner.clamp(Vector2(), Vector2(available - size).max(Vector2()));
    palette_wire_point = point;
    popup_in_workspace(palette, corner, size);
    static_cast<VisualScriptCanvas *>(canvas)->refresh_picker_wire();
    search->grab_focus();
}

void VisualScriptWindow::popup_in_workspace(Window *popup, Vector2 point, Vector2i size) {
    const Vector2 origin = get_global_transform_with_canvas().xform(point);
    popup->popup_on_parent(Rect2i(Vector2i(origin), size));
}

void VisualScriptWindow::palette_closed() {
    pending_pin = {};
    inserting_on_wire = false;
    static_cast<VisualScriptCanvas *>(canvas)->refresh_picker_wire();
    if (is_visible() && canvas->is_visible_in_tree() && !active_modal()) canvas->grab_focus();
}

bool VisualScriptWindow::palette_wire_geometry(Vector2 &from, Vector2 &to, Color &color) const {
    if (!palette->is_visible() || pending_pin.pin.is_empty() || !canvas_ports_available()) return false;
    Vector2 fixed;
    if (!canvas_pin_position(pending_pin.node, pending_pin.pin, pending_pin.output, fixed)) return false;

    const Transform2D window_to_canvas = canvas->get_global_transform_with_canvas().affine_inverse();
    const Vector2 parent_position = palette->is_embedded() ? Vector2(palette->get_position()) :
            Vector2(palette->get_position() - get_window()->get_position());
    const Rect2 popup = window_to_canvas.xform(Rect2(parent_position, Vector2(palette->get_size())));
    const float padding = ui::pixels(canvas, ui::spacing::sm);
    const Vector2 inner_min = popup.position + Vector2(padding, padding);
    const Vector2 inner_max = (popup.get_end() - Vector2(padding, padding)).max(inner_min);
    const Vector2 interior = palette_wire_point.clamp(inner_min, inner_max);
    Vector2 tip = palette_wire_point;
    if (!popup.has_point(fixed)) {
        popup.intersects_segment(fixed, interior, &tip);
    } else {
        // Clamping can put the picker over the source in a narrow window.
        tip = palette_wire_point.clamp(popup.position, popup.get_end());
    }

    from = pending_pin.output ? fixed : tip;
    to = pending_pin.output ? tip : fixed;
    color = pin_appearance(pending_pin.picker_descriptor()).ink;
    return true;
}

Vector2i VisualScriptWindow::palette_extent() {
    const Vector2i available(get_size());
    // Keep the popup and its text focus stable while filtering expands or collapses result groups.
    return Vector2i(std::min(available.x, ui::pixels(this, 356)), std::min(available.y, ui::pixels(this, 338)));
}

void VisualScriptWindow::build_graph_menus(Node *toolbar) {
    auto *tools_button = ui::menu(toolbar, "Graph Tools", "Search, frame, and configure graph wiring.");
    tools_button->set_name("GraphTools");
    tools_button->set_theme_type_variation("GraphToolbarButton");
    tools_button->set_custom_minimum_size(Vector2(ui::pixels(this, 76), ui::pixels(this, 28)));
    tools_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    tools_button->set_accessibility_name("Graph Tools");
    tools_button->set_disable_shortcuts(true);
    graph_tools_menu = tools_button->get_popup();
    tools_button->connect("about_to_popup", callable_mp(this, &VisualScriptWindow::refresh_graph_tools));
    graph_tools_menu->connect("id_pressed", callable_mp(this, &VisualScriptWindow::run_context_action).bind(graph_tools_menu));
    refresh_graph_tools();

    context_menu = add<PopupMenu>(this);
    context_menu->set_name("GraphContextMenu");
    context_menu->set_theme_type_variation("GraphCompactContextMenu");
    context_menu->set_allow_search(true);
    context_menu->connect("id_pressed", callable_mp(this, &VisualScriptWindow::run_context_action).bind(context_menu));
    context_menu->connect("popup_hide", callable_mp(static_cast<Control *>(canvas), &Control::grab_focus).bind(false));

    alignment_menu = add<PopupMenu>(context_menu);
    alignment_menu->set_name("GraphAlignmentMenu");
    alignment_menu->set_theme_type_variation("GraphCompactContextMenu");
    alignment_menu->connect("id_pressed", callable_mp(this, &VisualScriptWindow::run_context_action).bind(alignment_menu));
    for (const auto &[label, command] : {std::pair{"Straighten Connections", LayoutCommand::STRAIGHTEN},
                 {"Align Left", LayoutCommand::ALIGN_LEFT}, {"Align Center", LayoutCommand::ALIGN_CENTER},
                 {"Align Right", LayoutCommand::ALIGN_RIGHT}, {"Align Top", LayoutCommand::ALIGN_TOP},
                 {"Align Middle", LayoutCommand::ALIGN_MIDDLE}, {"Align Bottom", LayoutCommand::ALIGN_BOTTOM},
                 {"Distribute Horizontally", LayoutCommand::DISTRIBUTE_HORIZONTAL},
                 {"Distribute Vertically", LayoutCommand::DISTRIBUTE_VERTICAL}, {"Snap to Grid", LayoutCommand::SNAP},
                 {"Arrange", LayoutCommand::ARRANGE}}) {
        menu_action(alignment_menu, label, record_from("layout", static_cast<int64_t>(command)),
                command == LayoutCommand::STRAIGHTEN ? KEY_Q : KEY_NONE);
    }
}

void VisualScriptWindow::refresh_graph_tools() {
    if (auto *button = Object::cast_to<MenuButton>(graph_tools_menu->get_parent())) {
        const String help = autosave_error.is_empty() ? String("Search, frame, and configure graph wiring.") :
                String("Autosave paused. ") + autosave_error;
        ui::tooltip(button, help);
    }
    graph_tools_menu->clear();
    if (script.is_valid() && active_graph != 0) {
        menu_action(graph_tools_menu, "Open Event Graph", record_from("navigation", "event_graph"), KEY_NONE, "VisualShader");
    }
    menu_action(graph_tools_menu, "Paste Nodes", record_from("graph_tool", "paste"), static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_V), "ActionPaste");
    menu_action(graph_tools_menu, "Find Nodes", record_from("navigation", "find"), static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_F), "Search");
    menu_action(graph_tools_menu, "Frame Graph", record_from("navigation", "frame_graph"), KEY_HOME, "ZoomReset");
    if (script.is_valid() && !script->get_path().is_empty()) {
        menu_action(graph_tools_menu, "Reload from Disk", record_from("reload_graph", true), KEY_NONE, "Reload");
    }
    if (!autosave_error.is_empty()) {
        menu_action(graph_tools_menu, "Retry Autosave", record_from("retry_autosave", true), KEY_NONE, "StatusWarning");
    }
    graph_tools_menu->add_separator();
    menu_action(graph_tools_menu, "Cycle Wire Style", record_from("graph_tool", "wire_style"), KEY_NONE, "Reload");
    menu_action(graph_tools_menu, neutral_wires ? "Show Data Type Wire Colors" : "Use Neutral Wire Colors", record_from("graph_tool", "wire_colors"), KEY_NONE, "VisualShader");
    menu_action(graph_tools_menu, click_multi_connect ? "Turn Off Click Multi-Connect" : "Turn On Click Multi-Connect", record_from("graph_tool", "click_multi"), KEY_NONE, "link");
    menu_action(graph_tools_menu, automatic_connect ? "Turn Off Auto Connect" : "Turn On Auto Connect", record_from("graph_tool", "auto_connect"), KEY_NONE, "ExternalLink");
    menu_action(graph_tools_menu, automatic_insert ? "Turn Off Auto Insert" : "Turn On Auto Insert", record_from("graph_tool", "auto_insert"), KEY_NONE, "Add");
}

bool VisualScriptWindow::open_graph_context(Vector2 point) {
    if (script.is_null() || palette->is_visible() || context_menu->is_visible() || native_node_drag || native_wire_drag) {
        return false;
    }
    GraphNode *target = node_at_canvas_point(point);
    const PendingPin hit = debug_pin_hit(point, target);
    context_menu->clear();
    inserting_on_wire = false;
    pending_pin = {};
    hovered_wire.clear();
    hovered_wire_point = point;
    insertion_position = ((point + canvas->get_scroll_offset()) / canvas->get_zoom()).snapped(Vector2(16, 16));
    if (hit.node > 0 && (!target || hit.node == static_cast<int64_t>(target->get_meta(node_id_key, 0)))) {
        pending_pin = hit;
        const GraphNode *owner = widgets.at(hit.node);
        const Vector2 placement = owner->get_position_offset() +
                Vector2(hit.output ? owner->get_size().x + 48 : -224, 0);
        insertion_position = placement.snapped(Vector2(16, 16));
        add_pin_context_actions();
    } else if (target) {
        if (!target->is_selected()) {
            canvas->set_selected(target);
            canvas_selection_changed(target);
        }
        add_node_context_actions();
    } else {
        hovered_wire = canvas->get_closest_connection_at_point(point, ui::pixels(canvas, 7));
        if (hovered_wire.is_empty()) return false;
        menu_action(context_menu, "Insert Node on Wire", record_from("wire_action", "insert"), KEY_NONE, "Add");
        menu_action(context_menu, "Straighten Connection", record_from("wire_action", "straighten"), KEY_NONE, "ToolMove");
        menu_action(context_menu, "Reroute Wire", record_from("wire_action", "reroute"), KEY_NONE, "link");
        menu_action(context_menu, "Break Link", record_from("wire_action", "break"), KEY_NONE, "Unlinked");
    }
    canvas->grab_focus();
    const Vector2i position((get_global_transform_with_canvas().affine_inverse() *
            canvas->get_global_transform_with_canvas()).xform(point));
    const Vector2i room(get_size());
    context_menu->set_max_size(room);
    const Vector2i extent = Vector2i(context_menu->get_contents_minimum_size()).min(room);
    const Vector2i limit = (room - extent).max(Vector2i());
    const Vector2i corner = position.clamp(Vector2i(), limit);
    popup_in_workspace(context_menu, corner, extent);
    return true;
}

void VisualScriptWindow::add_pin_context_actions() {
    const bool execution = is_execution(pending_pin.descriptor);
    if (!execution) {
        const Dictionary address = debug_pin_address(pending_pin);
        if (!address.is_empty()) {
            bool watching = false;
            for (const Dictionary &watch : debug_watch_records(script->get_path())) {
                if (int64_t(watch.get("graph", -1)) == active_graph &&
                        int64_t(watch.get("node", 0)) == pending_pin.node &&
                        bool(watch.get("output", false)) == pending_pin.output &&
                        StringName(watch.get("pin", StringName())) == pending_pin.pin) {
                    watching = true;
                    break;
                }
            }
            menu_action(context_menu, watching ? "Remove Watch" : "Watch Pin",
                    record_from("watch", address, "enabled", !watching), KEY_NONE, watching ? "GuiVisibilityHidden" : "GuiVisibilityVisible");
        }
        menu_action(context_menu, "Promote to Variable", record_from("promote", false), KEY_NONE, "MemberProperty");
        if (!find_graph_record(graph_array(script->get_graph(), "functions"), active_graph).is_empty()) {
            menu_action(context_menu, "Promote to Local Variable", record_from("promote", true), KEY_NONE, "MemberProperty");
        }
        context_menu->add_separator();
    }
    menu_action(context_menu, "Break All Pin Links", record_from("graph_tool", "break_pin", "node", pending_pin.node,
            "pin", pending_pin.pin, "output", pending_pin.output), KEY_NONE, "Unlinked");
    if (!execution && !pending_pin.output) {
        menu_action(context_menu, "Reset Input to Default",
                record_from("graph_tool", "reset_pin", "node", pending_pin.node, "pin", pending_pin.pin), KEY_NONE, "Reload");
    }
    if (!execution) {
        const Dictionary graph = current_graph(script->get_graph());
        const Dictionary node = find_graph_record(graph_array(graph, "nodes"), pending_pin.node);
        if (!pending_pin.output && is_collection_constructor_input(node, pending_pin.pin)) {
            menu_action(context_menu, "Remove Pin", record_from("remove_collection_pin", pending_pin.pin,
                    "node", pending_pin.node), KEY_NONE, "Remove");
        }
        add_target_visibility_action(pending_pin.node, node, graph_array(graph, "connections"));
    } else if (pending_pin.output) {
        const auto widget = widgets.find(pending_pin.node);
        const Dictionary graph = current_graph(script->get_graph());
        const Dictionary node = find_graph_record(graph_array(graph, "nodes"), pending_pin.node);
        if (widget != widgets.end() && widget->second->get_output_port_count() > 1 &&
                StringName(node.get("kind", StringName())) == StringName("sequence")) {
            menu_action(context_menu, "Remove Pin", record_from("remove_sequence_pin", pending_pin.pin,
                    "node", pending_pin.node), KEY_NONE, "Remove");
        }
    }
}

void VisualScriptWindow::add_node_context_actions() {
    menu_action(context_menu, "Delete", record_from("delete", true), KEY_DELETE, "Remove");
    menu_action(context_menu, "Cut", record_from("graph_tool", "cut"), static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_X), "ActionCut");
    menu_action(context_menu, "Copy", record_from("graph_tool", "copy"), static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_C), "ActionCopy");
    menu_action(context_menu, "Duplicate", record_from("graph_tool", "duplicate"), static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_D), "Duplicate");
    menu_action(context_menu, "Duplicate with Inputs", record_from("graph_tool", "duplicate_inputs"), static_cast<Key>(KEY_MASK_ALT | KEY_V), "Duplicate");
    context_menu->add_separator();
    menu_action(context_menu, "Collapse to Function", record_from("collapse", "function"), KEY_NONE, "MemberMethod");
    menu_action(context_menu, "Collapse to Macro", record_from("collapse", "macro"), KEY_NONE, "Collapse");
    context_menu->add_submenu_node_item("Alignment", alignment_menu);
    set_graph_menu_icon(context_menu, context_menu->get_item_count() - 1, "ToolMove");
    menu_action(context_menu, "Create Comment from Selection", record_from("graph_tool", "comment"), KEY_C, "VisualShaderNodeComment");
    menu_action(context_menu, "Frame Selection", record_from("navigation", "frame_selection"), KEY_F, "ZoomReset");
    if (has_selected_declaration()) {
        menu_action(context_menu, "Go to Definition", record_from("navigation", "definition"), KEY_NONE, "ExternalLink");
        menu_action(context_menu, "Find References", record_from("navigation", "references"), KEY_NONE, "Search");
    }
    context_menu->add_separator();
    for (const auto &[label, command, glyph] : {std::tuple{"Break All Node Links", "break_nodes", "Unlinked"},
                 {"Connect Selected Nodes", "connect_selected", "link"}, {"Delete and Reconnect", "reconnect", "Reload"},
                 {"Bypass and Copy", "bypass_copy", "ActionCopy"}, {"Bypass and Keep", "bypass", "link"},
                 {"Select Upstream", "upstream", "MoveLeft"}, {"Select Downstream", "downstream", "MoveRight"}}) {
        menu_action(context_menu, label, record_from("graph_tool", command), KEY_NONE, glyph);
    }
    const Dictionary document = script->get_graph();
    const Dictionary graph = current_graph(document);
    const Array nodes = graph_array(graph, "nodes");
    const PackedInt64Array selection = selected_nodes();
    if (selection.size() == 1) {
        const Dictionary node = find_graph_record(nodes, selection[0]);
        if (String(node.get("kind", "")) == "reroute") {
            menu_action(context_menu, "Dissolve Reroute", record_from("graph_tool", "dissolve"), KEY_NONE, "Unlinked");
        }
        add_target_visibility_action(selection[0], node, graph_array(graph, "connections"));
    }
    bool can_disable = false;
    bool can_enable = false;
    bool can_add_breakpoint = false;
    bool can_remove_breakpoint = false;
    for (const int64_t node_id : selection) {
        const Dictionary node = find_graph_record(nodes, node_id);
        const bool disabled = node.get("disabled", false);
        can_enable |= disabled;
        can_disable |= !disabled && can_disable_graph_node(document, graph, node, script->get_graph_base());
        const bool has_breakpoint = debug_has_breakpoint(node_id);
        can_add_breakpoint |= !has_breakpoint && can_break_at_node(document, graph, node);
        can_remove_breakpoint |= has_breakpoint;
    }
    if (can_disable || can_enable) {
        context_menu->add_separator();
        if (can_disable) menu_action(context_menu, "Disable (Do Not Compile)", record_from("disable", true), KEY_NONE, "GuiVisibilityHidden");
        if (can_enable) menu_action(context_menu, "Enable Compile", record_from("disable", false), KEY_NONE, "GuiVisibilityVisible");
    }
    if (can_add_breakpoint || can_remove_breakpoint) {
        context_menu->add_separator();
        if (can_add_breakpoint) menu_action(context_menu, "Add Breakpoint", record_from("breakpoint", true),
                can_remove_breakpoint ? KEY_NONE : KEY_F9, "Breakpoint");
        if (can_remove_breakpoint) menu_action(context_menu, "Remove Breakpoint", record_from("breakpoint", false), KEY_F9, "Breakpoint");
    }
}

void VisualScriptWindow::add_target_visibility_action(int64_t node_id, const Dictionary &node, const Array &connections) {
    if (!has_implicit_property_target(script, node, connections)) {
        return;
    }
    const bool visible = node.get("show_target", false);
    context_menu->add_separator();
    menu_action(context_menu, visible ? "Hide Target" : "Show Target",
            record_from("target_visibility", node_id, "visible", !visible), KEY_NONE,
            visible ? "GuiVisibilityHidden" : "GuiVisibilityVisible");
}

void VisualScriptWindow::set_target_visible(int64_t node_id, bool visible) {
    Dictionary graph = current_graph(script->get_graph());
    Array nodes = graph_array(graph, "nodes").duplicate();
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        if (int64_t(node.get("id", 0)) != node_id) {
            continue;
        }
        if (!has_implicit_property_target(script, node, graph_array(graph, "connections"))) {
            return;
        }
        node = node.duplicate();
        if (visible) {
            node["show_target"] = true;
        } else {
            node.erase("show_target");
        }
        nodes[index] = node;
        graph["nodes"] = nodes;
        commit_graph(visible ? "Show Property Target" : "Hide Property Target", graph);
        return;
    }
}

void VisualScriptWindow::run_context_action(int64_t id, PopupMenu *menu) {
    const int32_t index = menu->get_item_index(id);
    if (script.is_null() || canvas_input_blocked() || index < 0 || menu->get_item_metadata(index).get_type() != Variant::DICTIONARY) return;
    const Dictionary command = menu->get_item_metadata(index);
    menu->hide();
    if (context_menu->is_visible()) context_menu->hide();
    canvas->grab_focus();
    if (command.has("graph_tool")) {
        run_graph_tool(command);
        if (menu == graph_tools_menu) refresh_graph_tools();
    } else if (command.has("navigation")) {
        navigation_palette_action(command);
    } else if (command.has("reload_graph")) {
        request_reload();
    } else if (command.has("retry_autosave")) {
        save_graph();
    } else if (command.has("delete")) {
        TypedArray<StringName> names;
        for (const int64_t node_id : selected_nodes()) names.append(widget_name(node_id));
        for (const auto &[node_id, frame] : comment_frames) {
            if (frame->is_selected()) names.append(frame->get_name());
        }
        delete_selected(names);
    } else if (command.has("layout")) {
        layout_selection(static_cast<LayoutCommand>(static_cast<int64_t>(command["layout"])));
    } else if (command.has("collapse")) {
        collapse_selection(String(command["collapse"]) == "macro");
    } else if (command.has("disable")) {
        set_selection_disabled(command["disable"]);
    } else if (command.has("breakpoint")) {
        set_selection_breakpoints(command["breakpoint"]);
    } else if (command.has("target_visibility")) {
        set_target_visible(command["target_visibility"], command["visible"]);
    } else if (command.has("remove_collection_pin")) {
        edit_collection_constructor(command["node"], command["remove_collection_pin"]);
    } else if (command.has("remove_sequence_pin")) {
        edit_sequence_pins(command["node"], command["remove_sequence_pin"]);
    } else if (command.has("watch")) {
        debug_watch_set(command["watch"], command["enabled"]);
    } else if (command.has("promote")) {
        promote_pin(command["promote"]);
    } else if (command.has("wire_action")) {
        const String action = command["wire_action"];
        const Dictionary wire = hovered_wire;
        if (action == "insert") {
            open_wire_insert();
        } else if (action == "break") {
            disconnect_nodes(wire.get("from_node", StringName()), wire.get("from_port", -1),
                    wire.get("to_node", StringName()), wire.get("to_port", -1));
        } else if (action == "straighten") {
            straighten_connections(wire);
        } else if (action == "reroute") {
            reroute_wire(hovered_wire_point);
        }
    }
}

void VisualScriptWindow::set_selection_disabled(bool disabled) {
    const Dictionary document = script->get_graph();
    Dictionary graph = current_graph(document);
    Array nodes = graph_array(graph, "nodes").duplicate();
    const PackedInt64Array selection = selected_nodes();
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        const int64_t id = node.get("id", 0);
        const bool was_disabled = node.get("disabled", false);
        if (!selection.has(id) || was_disabled == disabled ||
                (disabled && !can_disable_graph_node(document, graph, node, script->get_graph_base()))) continue;
        node = node.duplicate();
        if (disabled) {
            node["disabled"] = true;
        } else {
            node.erase("disabled");
        }
        nodes[index] = node;
    }
    graph["nodes"] = nodes;
    commit_graph(disabled ? "Disable Graph Nodes" : "Enable Graph Nodes", graph);
}

bool VisualScriptWindow::can_break_at_node(const Dictionary &document, const Dictionary &graph, const Dictionary &node) const {
    if (node.is_empty() || node.get("disabled", false) || String(node.get("kind", "")) == "reroute") return false;
    const StringName base = script->get_graph_base();
    for (const bool output : {false, true}) {
        for (const Dictionary &pin : graph_pins(document, graph, node, output, base)) {
            if (is_execution(pin)) return true;
        }
    }
    return false;
}

void VisualScriptWindow::set_selection_breakpoints(bool enabled) {
    const Dictionary document = script->get_graph();
    const Dictionary graph = current_graph(document);
    const Array nodes = graph_array(graph, "nodes");
    for (const int64_t id : selected_nodes()) {
        if (!enabled || can_break_at_node(document, graph, find_graph_record(nodes, id))) {
            debug_set_breakpoint(id, enabled);
        }
    }
    refresh_debug_markers();
}

void VisualScriptWindow::show_center_palette() {
    inserting_on_wire = false;
    pending_pin = {};
    hovered_wire.clear();
    open_palette(canvas->get_size() / 2.0);
}

Dictionary VisualScriptWindow::variable_drop_node(const Array &options, Vector2 point, PendingPin &target) const {
    if (script.is_null() || options.size() != 2 || !Rect2(Vector2(), canvas->get_size()).has_point(point)) {
        return {};
    }
    const Dictionary declaration = options[0];
    const Dictionary getter = declaration.get("node", Dictionary());
    const Dictionary setter = Dictionary(options[1]).get("node", Dictionary());
    if (String(getter.get("kind", String())) != "get_variable" || String(setter.get("kind", String())) != "set_variable") {
        return {};
    }
    const bool local = String(getter.get("scope", String())) == "local";
    if (uint64_t(declaration.get("source_script", 0)) != script->get_instance_id() ||
            (local && int64_t(declaration.get("source_graph", -1)) != active_graph)) {
        return {};
    }
    const Dictionary &document = panels->document_snapshot();
    const Dictionary graph = current_graph(document);
    const bool setting = Input::get_singleton()->is_key_pressed(KEY_CTRL);
    const Dictionary node = setting ? setter : getter;
    if (find_graph_record(graph_array(local ? graph : document, local ? "locals" : "variables"), getter.get("variable", 0)).is_empty()) {
        return {};
    }
    target = debug_pin_hit(point, node_at_canvas_point(point));
    if (!target.pin.is_empty() && (is_execution(target.descriptor) || target.output != setting ||
            insertion_pin(document, graph, node, !target.output, target.descriptor, script->get_graph_base()).is_empty())) {
        return {};
    }
    return node;
}

void VisualScriptWindow::preview_variable_drop(Vector2 point, const Array &options) {
    PendingPin target;
    std::optional<Vector2> snapped;
    const Dictionary node = variable_drop_node(options, point, target);
    const auto found = widgets.find(target.node);
    if (!node.is_empty() && found != widgets.end()) {
        auto *widget = found->second;
        const int32_t port = pin_index(widget, target.output, target.pin);
        if (port >= 0) {
            const Vector2 local = target.output ? widget->get_output_port_position(port) : widget->get_input_port_position(port);
            snapped = widget->get_position() + local * canvas->get_zoom();
        }
    }
    static_cast<VisualScriptCanvas *>(canvas)->set_compatible_socket(snapped);
}

void VisualScriptWindow::show_dropped_options(Vector2 point, const Array &options) {
    if (options.is_empty() || script.is_null() || canvas_input_blocked()) {
        return;
    }
    if (!point.is_finite()) {
        point = canvas->get_size() / 2.0;
    }
    pending_pin = {};
    hovered_wire.clear();
    inserting_on_wire = false;
    Dictionary node;
    if (options.size() == 1) {
        node = Dictionary(options[0]).get("node", Dictionary());
    } else if (options.size() == 2) {
        const Dictionary declaration = options[0];
        const Dictionary getter = declaration.get("node", Dictionary());
        const Dictionary setter = Dictionary(options[1]).get("node", Dictionary());
        if (String(getter.get("kind", String())) == "get_variable" && String(setter.get("kind", String())) == "set_variable") {
            PendingPin target;
            node = variable_drop_node(options, point, target);
            if (node.is_empty()) return;
            if (!target.pin.is_empty()) {
                pending_pin = target;
                // Keep the dropped card clear of the destination, on its connected side.
                const float zoom = canvas->get_zoom();
                const Vector2 offset(target.output ? 36.0f : -180.0f, -18.0f);
                insertion_position = ((point + canvas->get_scroll_offset()) / zoom + offset).snapped(Vector2(16, 16));
                insert_node(node, true);
                return;
            }
        }
    }
    const Vector2 position = ((point + canvas->get_scroll_offset()) / canvas->get_zoom()).snapped(Vector2(16, 16));
    if (!node.is_empty()) {
        insertion_position = position;
        insert_node(node);
    } else {
        // The native drag source still owns release while Godot calls _drop_data.
        const Vector2 origin = (get_global_transform_with_canvas().affine_inverse() *
                canvas->get_global_transform_with_canvas()).xform(point);
        callable_mp(this, &VisualScriptWindow::open_dropped_options).call_deferred(origin, position, options, script, active_graph);
    }
}

void VisualScriptWindow::open_dropped_options(Vector2 origin, Vector2 position, const Array &options, const Ref<VisualScript> &owner, int64_t graph) {
    if (!is_visible_in_tree() || script != owner || active_graph != graph || canvas_input_blocked()) return;
    drop_choice = show_drop_choice(this, origin, options,
            callable_mp(this, &VisualScriptWindow::accept_drop_choice).bind(position, owner, graph),
            callable_mp(this, &VisualScriptWindow::close_drop_choice));
}

void VisualScriptWindow::accept_drop_choice(const Dictionary &node, Vector2 position, const Ref<VisualScript> &owner, int64_t graph) {
    if (!drop_choice) return;
    close_drop_choice();
    if (script != owner || active_graph != graph || !is_visible_in_tree()) return;
    pending_pin = {};
    insertion_position = position;
    insert_node(node);
}

void VisualScriptWindow::close_drop_choice() {
    if (!drop_choice) return;
    Control *closing = drop_choice;
    drop_choice = nullptr;
    closing->hide();
    closing->queue_free();
    if (canvas->is_visible_in_tree()) canvas->grab_focus();
}

void VisualScriptWindow::drop_asset(Vector2 point, const String &path) {
    if (script.is_null() || canvas_input_blocked() || !point.is_finite()) return;
    const Ref<Resource> asset = ResourceLoader::get_singleton()->load(path);
    const Dictionary reference = make_preload_reference(asset);
    if (reference.is_empty()) {
        show_problem("The dropped file could not be loaded as a saved project asset.");
        return;
    }
    const StringName type = asset->get_class();
    const Dictionary preload = record_from("kind", "preload_resource", "class", "Resource", "asset_class", type,
            "values", record_from("asset", reference));
    Array options = Array::make(record_from("label", "Preload Resource", "node", preload,
            "help", "Load this saved resource when the graph is prepared."));
    if (type == StringName("PackedScene")) {
        for (const Dictionary &entry : find_native_members(type)) {
            const Dictionary member = entry.get("member", Dictionary());
            if (StringName(entry.get("kind", StringName())) != StringName("call_method") ||
                    StringName(member.get("name", StringName())) != StringName("instantiate")) continue;
            Dictionary instantiate = entry.duplicate();
            instantiate["values"] = record_from("target", reference);
            options.append(record_from("label", "Instantiate", "node", instantiate,
                    "help", "Create a node from this scene when execution reaches this call. Wire the result to Add Child to add it to the tree."));
            break;
        }
    }
    const Ref<Script> script_asset = asset;
    const Dictionary constructor = describe_script_constructor(script_asset);
    if (!constructor.is_empty()) {
        options.append(record_from("label", "Create from Script", "node", constructor,
                "help", "Call the script constructor. Use Add Child to put a new node in the scene."));
    }
    show_dropped_options(point, options);
}

void VisualScriptWindow::show_output_palette(StringName node, int64_t port, Vector2 point) {
    inserting_on_wire = false;
    hovered_wire.clear();
    const int64_t id = widget_id(canvas, node);
    const auto widget = widgets.find(id);
    if (widget != widgets.end()) {
        pending_pin = {id, pin_id(widget->second, true, port), pin_descriptor(widget->second, true, static_cast<int32_t>(port)), true};
        if (try_lazy_connection(pending_pin, point)) { pending_pin = {}; return; }
        open_palette(point);
    }
}

void VisualScriptWindow::show_input_palette(StringName node, int64_t port, Vector2 point) {
    inserting_on_wire = false;
    hovered_wire.clear();
    const int64_t id = widget_id(canvas, node);
    const auto widget = widgets.find(id);
    if (widget != widgets.end()) {
        pending_pin = {id, pin_id(widget->second, false, port), pin_descriptor(widget->second, false, static_cast<int32_t>(port)), false};
        if (try_lazy_connection(pending_pin, point)) { pending_pin = {}; return; }
        open_palette(point);
    }
}

void VisualScriptWindow::filter_palette(const String &text) {
    const String query = text.strip_edges();
    results->clear();
    auto *root = results->create_item();
    TreeItem *first = nullptr;
    const Dictionary document = script.is_valid() ? script->get_graph() : Dictionary();
    const Dictionary graph = current_graph(document);
    const Array graph_nodes = graph_array(graph, "nodes");
    const StringName base = script.is_valid() ? script->get_graph_base() : StringName("Node");
    const bool free = pending_pin.pin.is_empty();
    const bool filter_pin = palette_filters_pin();
    const bool pending_exec = is_execution(pending_pin.picker_descriptor());
    const bool can_create_node = !filter_pin || pending_exec ||
            (!pending_pin.output && static_cast<int64_t>(pending_pin.picker_descriptor().get("type", -1)) == Variant::OBJECT);
    std::map<String, TreeItem *> groups;
    std::map<std::pair<String, String>, TreeItem *> sections;
    const auto offer = [&](const String &group, const String &label, const String &help, const Dictionary &record,
                               const String &search_terms = String(), const String &section = String()) -> TreeItem * {
        if (inserting_on_wire && find_splice_pins(document, active_graph, saved_wire(hovered_wire), record, base).is_empty()) {
            return nullptr;
        }
        if ((!matches_query(label, query) && !matches_query(group, query) && !matches_query(section, query) &&
                    !matches_query(search_terms, query)) ||
                (filter_pin && insertion_pin(document, graph, record, !pending_pin.output, pending_pin.descriptor, base,
                        pending_pin.presentation ? &*pending_pin.presentation : nullptr).is_empty())) {
            return nullptr;
        }
        auto *&group_item = groups[group];
        if (!group_item) {
            group_item = category(results, root, group);
        }
        auto *parent = group_item;
        if (!section.is_empty()) {
            auto *&section_item = sections[{group, section}];
            if (!section_item) {
                section_item = category(results, group_item, section);
                section_item->set_collapsed(query.is_empty());
            }
            parent = section_item;
        }
        auto *item = action(results, parent, label, record.get("kind", StringName()), help);
        item->set_metadata(0, record);
        if ((group == "Scene" || group == "Debug") && !query.is_empty() && label.nocasecmp_to(query) == 0) {
            first = item;
        } else {
            prefer_result(first, item, query);
        }
        return item;
    };
    if (receiver_member_count > 0) {
        add_members(root, document, graph, base, query, first);
    }
    if (!filter_pin && !inserting_on_wire && active_graph == 0 && matches_query("Add Custom Event", query)) {
        auto *item = action(results, root, "Add Custom Event", "custom_event", "Create an event entry and edit its name and inputs in Details.");
        item->set_metadata(0, record_from("create_event", true));
        prefer_result(first, item, query);
    }
    if (!free && !pending_exec) {
        const auto promotion = [&](const String &label, bool local) {
            if (!matches_query(label, query)) return;
            auto *item = action(results, root, label, "promote", "Create a typed variable and connect this pin to it.");
            item->set_metadata(0, record_from("promote", local));
            prefer_result(first, item, query);
        };
        promotion("Promote to Variable", false);
        if (!find_graph_record(graph_array(document, "functions"), active_graph).is_empty()) {
            promotion("Promote to Local Variable", true);
        }
    }
    if (active_graph == 0) {
        for (const Dictionary &event : lifecycle_events()) {
            const StringName callback = event.get("callback", StringName());
            const bool ready = callback == StringName("_ready");
            const bool exists = std::any_of(graph_nodes.begin(), graph_nodes.end(), [&](const Variant &item) {
                const Dictionary node = item;
                return (ready && StringName(node.get("kind", StringName())) == StringName(ready_kind)) ||
                        StringName(node.get("callback", StringName())) == callback;
            });
            if (!exists) {
                offer("Events", event.get("title", String()), visual_script_method_help("Node", callback),
                        ready ? record_from("kind", ready_kind) : record_from("kind", "lifecycle", "callback", callback));
            }
        }
    }
    if (receiver_member_count == 0 && !member_options.is_empty()) {
        add_members(root, document, graph, base, query, first);
    }
    offer("Debug", "Print", "Write a message to Godot's Output panel.", record_from("kind", print_kind));
    offer("Scripting", "Custom Script", "Run a GDScript body with typed graph inputs and outputs.",
            record_from("kind", custom_script_kind, "code", "pass", "inputs", Array(), "outputs", Array()));
    offer("Scene", "Add Child", "Parent a constructed node under the graph owner or an explicitly connected parent.",
            record_from("kind", add_child_kind));
    offer("Scene", "Self", "Get the exact node that runs this script.", record_from("kind", "self"));
    if (!inserting_on_wire && (!filter_pin || (!pending_pin.output && !pending_exec))) {
        for (const Dictionary &singleton : singleton_options) {
            const String name = singleton.get("singleton", String());
            const StringName source = singleton.get("source", StringName());
            const String section = source == StringName("autoload") ? "Autoloads" : "Engine";
            offer("Singletons", name, singleton_help(singleton), singleton, "Get " + name, section);
        }
    }
    if (!free) {
        offer("Wires", "Reroute", "Insert a movable dot for this wire type.",
                record_from("kind", "reroute", "mode", pending_exec ? "exec" : "data", "pin", pending_pin.descriptor));
    }
    for (const char *kind : {"branch", "sequence", "switch", "while", "for", "foreach", "break", "continue",
                 "do_once", "do_n", "flip_flop", "gate"}) {
        const String label = String(kind) == "foreach" ? String("For Each") : String(kind) == "do_n" ? String("Do N") : String(kind).capitalize();
        offer("Flow Control", label, "Choose how execution proceeds.", record_from("kind", kind));
    }
    if (active_graph == 0) {
        offer("Flow Control", "Delay", "Continue after the specified number of seconds.", record_from("kind", "delay"), "wait timer await");
        offer("Flow Control", "Await Signal", "Continue the next time the connected signal fires.",
                record_from("kind", "await_signal"), "wait signal asynchronous");
    }
    for (const Variant::Type value_type : supported_visual_value_types()) {
        const String type_name = Variant::get_type_name(value_type);
        for (const bool assemble : {true, false}) {
            const String prefix = assemble ? "Make " : "Break ";
            offer("Values", prefix + type_name, assemble ? "Build this value from its typed fields." : "Read each field of this value separately.",
                    record_from("kind", assemble ? "make_value" : "break_value", "value_type", static_cast<int64_t>(value_type)),
                    String(assemble ? "Compose " : "Decompose ") + type_name);
        }
    }
    for (const Dictionary &poll : find_input_poll_nodes()) {
        offer("Input Actions", poll.get("label", String()), poll.get("description", String("Read an action from Godot's Input Map.")), poll);
    }
    for (const auto &[kind, label, help] : {
                 std::tuple{"make_array", "Make Array", "Build an array from the input items."},
                 std::tuple{"make_dictionary", "Make Dictionary", "Build a dictionary from the input pairs."},
                 std::tuple{"get_array_item", "Get Array Item", "Read the item at an index and check whether it exists."},
                 std::tuple{"get_dictionary_value", "Get Dictionary Value", "Read the value for a key and check whether it exists."},
                 std::tuple{"set_array_item", "Set Array Item", "Return a copy with the item replaced."},
                 std::tuple{"append_array_item", "Append Array Item", "Return a copy with one item appended."},
                 std::tuple{"remove_array_item", "Remove Array Item", "Return a copy without the item at an index."},
                 std::tuple{"set_dictionary_value", "Set Dictionary Value", "Return a copy with the key and value set."},
                 std::tuple{"erase_dictionary_key", "Erase Dictionary Key", "Return a copy without the key."}}) {
        const String category_name = String(kind).contains("array") ? "Arrays" : "Dictionaries";
        Dictionary node = record_from("kind", kind);
        if (String(kind) == "make_dictionary") node["dictionary_types"] = Dictionary();
        offer("Collections", label, help, node, String(), category_name);
    }
    const StringName target_class = pending_pin.picker_descriptor().get("class", StringName());
    const bool target_resource = !pending_pin.output && !target_class.is_empty() && ClassDB::is_parent_class(target_class, "Resource");
    const StringName resource_class = target_resource ? target_class : StringName("Resource");
    if (!inserting_on_wire && (!filter_pin || (!pending_pin.output && !pending_exec))) {
        offer("Resources", "Preload Resource", "Load a saved project asset when the graph is prepared.",
                record_from("kind", "preload_resource", "class", resource_class), "preload packed scene texture script");
    }
    offer("Resources", "Load Resource", "Load a resource from a path when execution reaches this node. Use Failure if the resource is unavailable.",
            record_from("kind", "load_resource", "class", resource_class), "load scene resource path");
    if (!inserting_on_wire && !filter_pin && matches_query("Create from Script", query)) {
        auto *item = action(results, root, "Create from Script", "create_node", "Choose a saved GDScript and expose its constructor arguments.");
        item->set_metadata(0, record_from("choose_script_constructor", true));
        prefer_result(first, item, query);
    }
    const StringName receiver = pending_pin.picker_descriptor().get("class", StringName());
    const bool resource_receiver = !free && pending_pin.output && !receiver.is_empty() && ClassDB::is_parent_class(receiver, "Resource");
    if (!resource_receiver && (!filter_pin || !pending_pin.output) &&
            (matches_query("Get Resource Path", query) || matches_query("Resources", query))) {
        for (const Dictionary &option : find_native_members("Resource")) {
            const Dictionary member = option.get("member", Dictionary());
            if (StringName(option.get("kind", StringName())) == StringName("get_property") &&
                    StringName(member.get("name", StringName())) == StringName("resource_path")) {
                offer("Resources", "Get Resource Path", "Read the project path of a Resource.", option);
                break;
            }
        }
    }
    if (!filter_pin || pending_exec || (static_cast<int64_t>(pending_pin.picker_descriptor().get("type", Variant::NIL)) == Variant::OBJECT) ||
            static_cast<int64_t>(pending_pin.picker_descriptor().get("type", Variant::NIL)) == Variant::NIL) {
        // Pick only related targets from a known Object source; Context Sensitive off can show every target.
        const auto offer_cast = [&](const String &name, const Dictionary &target, const String &section) {
            const String label = "Cast To " + name;
            if (!matches_query(label, query) && !matches_query("Type Casting", query) && !matches_query(section, query)) return;
            if (filter_pin && pending_pin.output && !pending_exec && !possible_object_cast(pending_pin.picker_descriptor(), target)) return;
            offer("Type Casting", label, "Check the object's type and continue through Success or Failure.",
                    record_from("kind", "cast", "pin", target, "label", label), String(), section);
        };
        for (const String &class_name : native_cast_types) {
            offer_cast(class_name, make_pin("result", "As " + class_name, Variant::OBJECT, class_name), "Godot");
        }
        for (const Dictionary &target : script_cast_types) {
            const String script_name = target.get("script_name", String());
            if (!script_name.is_empty()) offer_cast(script_name, target, "Script References");
        }
    }
    if (!free && pending_pin.output && !pending_exec) {
        const StringName enum_type = pending_pin.picker_descriptor().get("enum_type", StringName());
        const String authored_name = authored_enum_label(document, enum_type);
        const Dictionary enum_switch = authored_name.is_empty() ? make_enum_switch(enum_type) : make_authored_enum_switch(document, enum_type);
        const String label = "Switch on " + (authored_name.is_empty() ? String(enum_type).get_slice(".", 1).capitalize() : authored_name);
        if (!enum_switch.is_empty() && (matches_query(label, query) || matches_query("Switch on Enum", query))) {
            auto *item = action(results, root, label, "switch_enum", "Choose an execution path for each named enum value.");
            item->set_metadata(0, enum_switch);
            prefer_result(first, item, query);
        }
    }
    if (!filter_pin && !inserting_on_wire) {
        const StringName pin_enum = pending_pin.output ? StringName(pending_pin.picker_descriptor().get("enum_type", StringName())) : StringName();
        TreeItem *enum_group = nullptr;
        for (const Dictionary &option : authored_enum_options(document)) {
            if (StringName(option["id"]) == pin_enum) continue;
            const String label = "Switch on " + String(option["name"]);
            if (!matches_query(label, query) && !matches_query("Switch on Enum", query)) continue;
            if (!enum_group) enum_group = category(results, root, "Switch on Enum");
            auto *item = action(results, enum_group, label, "switch_enum", "Choose a path for each case in this script's enum.");
            item->set_metadata(0, record_from("create_enum_switch", option["id"]));
            prefer_result(first, item, query);
        }
        const bool search_native_enums = !query.is_empty() && (query.containsn("switch") || query.containsn("enum"));
        if (search_native_enums && native_enum_options.is_empty()) {
            PackedStringArray owners = ClassDB::get_class_list();
            owners.sort();
            for (const String &owner : owners) {
                PackedStringArray enumerations = ClassDB::class_get_enum_list(owner, true);
                enumerations.sort();
                for (const String &enumeration : enumerations) {
                    if (!ClassDB::is_class_enum_bitfield(owner, enumeration, true)) {
                        native_enum_options.append(record_from("label", "Switch on " + enumeration.capitalize(),
                                "owner", owner, "type", owner + String(".") + enumeration));
                    }
                }
            }
        }
        if (search_native_enums) {
            for (const Dictionary &option : native_enum_options) {
                const String label = option["label"];
                const String qualified = option["type"];
                if (!matches_query(label, query) && !matches_query("Switch on " + qualified, query) &&
                        !matches_query("Switch on Enum", query)) continue;
                if (!enum_group) enum_group = category(results, root, "Switch on Enum");
                auto *item = action(results, enum_group, label, "switch_enum", qualified);
                item->set_suffix(0, "   " + String(option["owner"]));
                item->set_metadata(0, record_from("create_enum_switch", qualified));
                prefer_result(first, item, query);
            }
        }
    }
    offer("Logic", "Select", "Return one of two values from a condition.", record_from("kind", "select"));
    for (const char *operation : {"add", "subtract", "multiply", "divide", "modulo", "equal", "not_equal", "less",
                 "less_equal", "greater", "greater_equal", "and", "or", "not", "negate"}) {
        Dictionary record = record_from("kind", "operator", "operator", operation);
        const String name = operation;
        if (name == "add" || name == "subtract" || name == "multiply" || name == "divide" || name == "modulo" || name == "negate") {
            Dictionary operand = make_pin("a", "A", name == "modulo" ? Variant::INT : Variant::FLOAT);
            operand["default"] = name == "modulo" ? Variant(0) : Variant(0.0);
            record["pin"] = operand;
        }
        offer("Operators", String(operation).capitalize(), "Evaluate this value operation when its result is read.",
                record);
    }
    for (const auto &[type, value] : {std::pair{Variant::BOOL, Variant(false)}, {Variant::INT, Variant(0)}, {Variant::FLOAT, Variant(0.0)},
                 {Variant::STRING, Variant(String())}, {Variant::VECTOR2, Variant(Vector2())}, {Variant::VECTOR3, Variant(Vector3())}}) {
        const String label = Variant::get_type_name(type);
        Dictionary literal = make_pin("value", "Value", type);
        literal["default"] = value;
        offer("Constants", label, "Read a fixed value.", record_from("kind", "literal", "pin", literal));
    }
    if (!inserting_on_wire && (!filter_pin || (!pending_pin.output && !pending_exec))) {
        const String group = "Global Constants";
        const Ref<Texture2D> icon = results->get_theme_icon("MemberConstant", "EditorIcons");
        for (size_t index = 0; index < global_constants_count; ++index) {
            const GlobalConstant &constant = global_constants[index];
            const String symbol = constant.name;
            const String section = constant.enumeration ? String(constant.enumeration) : String();
            const String global_name = String("@GlobalScope.") + symbol;
            const String qualified_name = section.is_empty() ? String() : String("@GlobalScope.") + section + "." + symbol;
            const String terms = global_name + String(" ") + qualified_name + String(" ") + symbol.replace("_", " ");
            if (!matches_query(symbol, query) && !matches_query(section, query) && !matches_query(group, query) &&
                    !matches_query(terms, query)) {
                continue;
            }
            if (auto *item = offer(group, symbol, global_constant_help(constant), make_global_constant(constant), terms, section)) {
                item->set_icon(0, icon);
            }
        }
    }
    for (const auto &[key, kind, category_name] : {std::tuple{"functions", "call_function", "Functions"},
                 std::tuple{"macros", "call_macro", "Macros"}, std::tuple{"events", "emit_event", "Custom Events"}}) {
        for (const Dictionary &definition : graph_array(document, key)) {
            const String name = definition.get("name", String());
            const int64_t id = definition.get("id", 0);
            const bool event = String(key) == "events";
            const String category = definition.get("category", String());
            const String group = event || category.is_empty() ? String(category_name) : String(category_name) + String(" / ") + category;
            const String description = definition.get("description", String());
            const String search_terms = String(definition.get("keywords", String())) + String(" ") +
                    String(definition.get("compact_title", String()));
            offer(group, event ? "Emit " + name : name, description.is_empty() ? String("Use a declaration from My Script.") : description,
                    record_from("kind", kind, "definition", id), search_terms);
            if (String(key) == "events" && active_graph == 0 && std::none_of(graph_nodes.begin(), graph_nodes.end(), [&](const Variant &item) {
                    const Dictionary node = item;
                    return String(node.get("kind", String())) == "custom_event" && static_cast<int64_t>(node.get("definition", 0)) == id;
                })) {
                offer(category_name, "On " + name,
                        description.is_empty() ? String("Run when this script instance emits the event.") : description,
                        record_from("kind", "custom_event", "definition", id));
            }
        }
    }
    for (const bool local : {false, true}) {
        for (const Dictionary &variable : graph_array(local ? graph : document, local ? "locals" : "variables")) {
            const String description = variable.get("description", String());
            for (const bool write : {false, true}) {
                const String name = variable.get("name", String());
                const String help = write ? "Change this variable when execution reaches this node." : "Read this variable's current value.";
                offer("Functions", (write ? "Set " : "Get ") + name,
                        description.is_empty() ? help : description,
                        record_from("kind", write ? "set_variable" : "get_variable", "scope", local ? "local" : "instance",
                                "variable", variable.get("id", 0)));
            }
        }
    }
    if (can_create_node && !inserting_on_wire && native_classes.find(StringName("Node")) != native_classes.end()) {
        const bool show_all_classes = query.is_empty() || matches_query("Create Node", query);
        std::map<StringName, bool> matches;
        std::function<bool(const StringName &)> mark = [&](const StringName &name) {
            const auto &entry = native_classes.at(name);
            const bool compatible = !filter_pin || pending_exec ||
                    pins_compatible(make_pin("node", String(name), Variant::OBJECT, name), pending_pin.picker_descriptor());
            bool visible = compatible && (show_all_classes || matches_query(String(name), query) || matches_query("Create " + String(name), query));
            for (const auto &child : entry.children) {
                visible |= mark(child);
            }
            matches.emplace(name, visible);
            return visible;
        };
        if (mark("Node")) {
            add_native_class(category(results, root, "Create Node"), "Node", matches, query, first);
        }
    }
    if (query.is_empty()) {
        for (TreeItem *item = root->get_first_child(); item; item = item->get_next()) {
            if (item->get_first_child()) {
                item->set_collapsed(true);
            }
        }
        if (!free && first && first->get_parent() == root) {
            first->select(0);
        }
    } else if (first) {
        first->select(0);
        results->scroll_to_item(first);
    } else {
        category(results, root, "No matching nodes");
    }
    palette_selection_changed();
}

void VisualScriptWindow::add_members(TreeItem *root, const Dictionary &document, const Dictionary &graph,
        const StringName &base, const String &query, TreeItem *&first) {
    const bool free = pending_pin.pin.is_empty();
    const bool filter_pin = palette_filters_pin();
    const String pin_type = free ? String() : type_label(pending_pin.picker_descriptor(), document);
    const StringName receiver_class = pending_pin.picker_descriptor().get("class", StringName());
    const String receiver_script = pending_pin.picker_descriptor().get("script_path", String());
    const Dictionary splice_wire = inserting_on_wire ? saved_wire(hovered_wire) : Dictionary();
    TreeItem *last_direct = nullptr;
    TreeItem *first_inherited = nullptr;
    TreeItem *builtin_group = nullptr;
    TreeItem *global_group = nullptr;
    TreeItem *static_group = nullptr;
    TreeItem *tag_group = nullptr;
    std::map<std::pair<TreeItem *, String>, TreeItem *> scopes;
    std::map<StringName, TreeItem *> inherited_classes;
    std::set<std::tuple<StringName, String, String>> receiver_members;
    int exact_match_priority = 0;
    for (int64_t index = 0; index < member_options.size(); ++index) {
        const Dictionary option = member_options[index];
        const bool has_receiver = index < receiver_member_count;
        const StringName kind = option.get("kind", StringName());
        const Dictionary member = option.get("member", Dictionary());
        const bool builtin = member.has("variant_type");
        const bool utility = member.get("utility", false);
        const bool static_method = member.get("static", false);
        const bool await = kind == StringName("await_signal");
        const bool signal = kind == StringName("native_signal") || await;
        const String native_name = signal ? option.get("signal", StringName()) : member.get("name", StringName());
        const String label = signal ? (await ? "Await " : "On ") + native_name.capitalize() : member_title(kind, member);
        const Dictionary &declaration_record = signal ? option : member;
        const String script_name = declaration_record.get("script_name", StringName());
        const StringName declaring_class = declaration_record.get("class", StringName());
        const String declaration = script_name.is_empty() ? String(declaring_class) : script_name;
        const String qualified_name = declaration + String(".") + native_name;
        if (has_receiver && static_method) {
            continue;
        }
        const bool native_static = static_method && !builtin && !utility && script_name.is_empty();
        const String tag_scope = script_name.is_empty() ? gameplay_tag_scope(declaring_class) : String();
        const String catalog = !tag_scope.is_empty() ? "Gameplay Tags" : utility ? "Global Functions" : builtin ? "Built-in Types" :
                native_static ? "Native Static Methods" : "Self";
        if (!matches_query(label, query) && !matches_query(declaration, query) && !matches_query(native_name, query) &&
                !matches_query(qualified_name, query) && !matches_query(catalog, query) &&
                !(has_receiver && matches_query(pin_type, query))) {
            continue;
        }
        if (inserting_on_wire && find_splice_pins(document, active_graph, splice_wire, option, base).is_empty()) {
            continue;
        }
        const bool compatible = !free && !insertion_pin(document, graph, option, !pending_pin.output, pending_pin.descriptor, base,
                        pending_pin.presentation ? &*pending_pin.presentation : nullptr).is_empty();
        if (filter_pin && !compatible) {
            continue;
        }
        const bool contextual = has_receiver && compatible;
        const bool direct = contextual && (builtin || (receiver_script.is_empty() ? declaring_class == receiver_class :
                String(declaration_record.get("script_path", String())) == receiver_script));
        const auto key = std::make_tuple(kind, declaration, native_name);
        if (contextual) {
            receiver_members.insert(key);
        } else if (receiver_members.find(key) != receiver_members.end()) {
            continue;
        }
        TreeItem *group = nullptr;
        if (direct) {
            group = root;
        } else if (contextual) {
            auto *&inherited = inherited_classes[declaring_class];
            if (!inherited) {
                inherited = category(results, root, declaration);
                if (!first_inherited) first_inherited = inherited;
            }
            group = inherited;
        } else if (utility) {
            if (!global_group) {
                global_group = category(results, root, "Global Functions");
            }
            group = global_group;
        } else {
            TreeItem *parent = root;
            if (!tag_scope.is_empty()) {
                if (!tag_group) tag_group = category(results, root, "Gameplay Tags");
                parent = tag_group;
            } else if (builtin) {
                if (!builtin_group) {
                    builtin_group = category(results, root, "Built-in Types");
                }
                parent = builtin_group;
            } else if (native_static) {
                if (!static_group) {
                    static_group = category(results, root, "Native Static Methods");
                }
                parent = static_group;
            }
            const String section = !tag_scope.is_empty() ? tag_scope : declaration;
            auto *&scope = scopes[{parent, section}];
            if (!scope) {
                scope = category(results, parent, section);
                scope->set_collapsed(query.is_empty() && parent != root);
            }
            group = scope;
        }
        String help = signal ? (script_name.is_empty() ? visual_script_signal_help(declaration, option.get("signal", StringName())) :
                qualified_name) : member_help(kind, member);
        if (await) {
            help = String("Continue when this signal next fires.") + (help.is_empty() ? String() : String("\n\n") + help);
        }
        auto *item = action(results, group, label, kind, help);
        if (direct) {
            if (first_inherited) item->move_before(first_inherited);
            last_direct = item;
        }
        {
            const Ref<Texture2D> icon = palette->get_theme_icon(signal ? "Signal" : "MemberMethod", "EditorIcons");
            if (icon.is_valid()) {
                item->set_icon(0, icon);
                item->set_icon_max_width(0, ui::pixels(this, 12));
                item->set_icon_modulate(0, signal ? node_accent(kind) : method_color(member));
            }
        }
        item->set_metadata(0, option);
        if (query.is_empty() && direct && !first) {
            first = item;
        } else if (!query.is_empty()) {
            const bool exact_name = native_name.nocasecmp_to(query) == 0;
            const bool exact_qualified = qualified_name.nocasecmp_to(query) == 0;
            const int priority = exact_qualified ? 3 : exact_name && utility ? 2 : exact_name ? 1 : 0;
            if (priority > exact_match_priority) {
                first = item;
                exact_match_priority = priority;
            } else if (exact_match_priority == 0) {
                prefer_result(first, item, query);
            }
        }
    }
    // Methods and signals are reflected separately. Order their shared categories by inheritance.
    TreeItem *previous = last_direct;
    for (StringName owner = receiver_class; !inherited_classes.empty() && !owner.is_empty(); owner = ClassDB::get_parent_class(owner)) {
        const auto found = inherited_classes.find(owner);
        if (found == inherited_classes.end()) continue;
        TreeItem *item = found->second;
        if (previous && previous->get_next() != item) {
            item->move_after(previous);
        } else if (!previous && item != root->get_first_child()) {
            item->move_before(root->get_first_child());
        }
        previous = item;
        inherited_classes.erase(found);
    }
}

TreeItem *VisualScriptWindow::add_native_class(TreeItem *parent, const StringName &name,
        const std::map<StringName, bool> &matches, const String &query, TreeItem *&first) {
    if (!matches.at(name)) {
        return nullptr;
    }
    const auto &entry = native_classes.at(name);
    bool compatible = !palette_filters_pin() || is_execution(pending_pin.descriptor);
    if (!compatible) {
        const Dictionary produced = make_pin("node", String(name), Variant::OBJECT, name);
        compatible = pins_compatible(produced, pending_pin.descriptor) && pins_compatible(produced, pending_pin.picker_descriptor());
    }
    const bool constructible = entry.constructible && compatible;
    TreeItem *item = constructible
            ? action(results, parent, String(name), "create_node", "Create a " + String(name) + " node at runtime.")
            : category(results, parent, String(name));
    if (constructible) {
        Dictionary metadata = item->get_metadata(0);
        metadata["class"] = name;
        item->set_metadata(0, metadata);
        if ((!first && (matches_query(String(name), query) || matches_query("Create " + String(name), query))) ||
                (!query.is_empty() && (String(name).nocasecmp_to(query) == 0 || ("Create " + String(name)).nocasecmp_to(query) == 0))) {
            first = item;
        }
    } else {
        item->set_tooltip_text(0, compatible
                ? String(name) + String(" cannot be created directly. Expand it to browse derived node types.")
                : String(name) + String(" does not match the selected input type."));
    }
    for (const auto &child : entry.children) {
        add_native_class(item, child, matches, query, first);
    }
    item->set_collapsed(query.is_empty() && name != StringName("Node"));
    return item;
}

void VisualScriptWindow::palette_selection_changed() {
    const auto *selected = results->get_selected();
    add_button->set_disabled(!selected || selected->get_metadata(0).get_type() == Variant::NIL);
}

void VisualScriptWindow::palette_row_clicked(Vector2, int64_t button) {
    const auto *item = results->get_selected();
    if (button == MOUSE_BUTTON_LEFT && item && item->get_metadata(0).get_type() == Variant::DICTIONARY) {
        const Dictionary metadata = item->get_metadata(0);
        callable_mp(this, &VisualScriptWindow::insert_palette_result).call_deferred(metadata);
    }
}

void VisualScriptWindow::palette_search_input(const Ref<InputEvent> &event) {
    const Ref<InputEventKey> key = event;
    if (!(key.is_valid() && key->is_pressed() && key->get_keycode() == KEY_DOWN) &&
            !event->is_action_pressed("ui_down", true)) return;
    results->grab_focus();
    if (!results->get_selected()) {
        for (TreeItem *row = results->get_root()->get_first_child(); row; row = row->get_next_visible()) {
            if (!row->is_selectable(0)) continue;
            row->select(0);
            results->scroll_to_item(row);
            break;
        }
    }
    search->accept_event();
}

void VisualScriptWindow::submit_search(const String &) {
    insert_selected();
}

void VisualScriptWindow::insert_selected() {
    const auto *selected = results->get_selected();
    if (selected && selected->get_metadata(0).get_type() == Variant::DICTIONARY) {
        insert_palette_result(selected->get_metadata(0));
    }
}

void VisualScriptWindow::insert_palette_result(const Dictionary &metadata) {
    if (script.is_null() || !palette->is_visible()) {
        return;
    }
    if (inserting_on_wire) {
        insert_node_on_wire(metadata);
        return;
    }
    if (metadata.get("choose_script_constructor", false)) {
        choose_script_constructor();
        return;
    }
    if (metadata.get("create_event", false)) {
        insert_custom_event();
        return;
    }
    if (metadata.has("create_enum_switch")) {
        const StringName enum_type = metadata["create_enum_switch"];
        const Dictionary enum_switch = authored_enum_id(enum_type) > 0
                ? make_authored_enum_switch(script->get_graph(), enum_type) : make_enum_switch(enum_type);
        if (enum_switch.is_empty()) {
            show_problem("This enum has no supported cases.");
        } else {
            insert_node(enum_switch);
        }
        return;
    }
    if (metadata.has("promote")) {
        promote_pin(metadata["promote"]);
        return;
    }
    insert_node(metadata);
}

void VisualScriptWindow::choose_script_constructor() {
    palette->hide();
    browse_project_resource(this, "GDScript", callable_mp(this, &VisualScriptWindow::insert_script_constructor)
            .bind(script, active_graph, insertion_position));
}

void VisualScriptWindow::insert_script_constructor(const String &path, const Ref<VisualScript> &owner, int64_t graph, Vector2 point) {
    if (script != owner || active_graph != graph) return;
    const Dictionary constructor = describe_script_constructor(load_script_type(path));
    if (constructor.is_empty()) {
        show_problem("Choose a GDScript with a concrete Node or RefCounted base and a supported constructor.");
        return;
    }
    insertion_position = point;
    pending_pin = {};
    insert_node(constructor);
}

void VisualScriptWindow::promote_pin(bool local) {
    if (script.is_null() || pending_pin.node <= 0 || is_execution(pending_pin.descriptor)) {
        return;
    }
    Dictionary document = script->get_graph();
    Dictionary graph = current_graph(document);
    const char *key = local ? "locals" : "variables";
    Array variables = graph_array(local ? graph : document, key).duplicate();
    Array nodes = graph_array(graph, "nodes");
    const int64_t id = next_node_id(nodes);
    const int64_t variable_id = next_node_id(variables);
    if (id == 0 || variable_id == 0) {
        show_problem("This script has no more graph or variable IDs.");
        return;
    }
    const String label = String(pending_pin.descriptor.get("label", "value")).to_snake_case();
    const String prefix = label.is_valid_identifier() ? label : String("value");
    String name = prefix;
    for (int64_t suffix = 2; std::any_of(variables.begin(), variables.end(), [&](const Variant &entry) {
                 return String(Dictionary(entry).get("name", String())) == name;
             }); ++suffix) {
        name = prefix + String("_") + String::num_int64(suffix);
    }
    Dictionary pin = pending_pin.descriptor.duplicate();
    pin["id"] = "value";
    pin["label"] = "Value";
    pin.erase("exec");
    const Dictionary origin = find_graph_record(nodes, pending_pin.node);
    const Dictionary values = origin.get("values", Dictionary());
    Variant initial = values.get(pending_pin.pin, pin.get("default", Variant()));
    const auto type = static_cast<Variant::Type>(static_cast<int64_t>(pin.get("type", Variant::NIL)));
    if (initial.get_type() == Variant::NIL) {
        if (type == Variant::STRING && String(origin.get("kind", String())) == print_kind) {
            initial = origin.get("message", String());
        } else if (type != Variant::OBJECT && type != Variant::NIL) {
            initial = UtilityFunctions::type_convert(Variant(), type);
        }
    }
    pin.erase("default");
    variables.append(record_from("id", variable_id, "name", name, "pin", pin, "value", initial));
    if (local) {
        graph[key] = variables;
    } else if (active_graph == 0) {
        graph[key] = variables;
        document[key] = variables;
    } else {
        document[key] = variables;
    }
    nodes.append(record_from("id", id, "kind", pending_pin.output ? "set_variable" : "get_variable", "position", insertion_position,
            "scope", local ? "local" : "instance", "variable", variable_id));
    graph["nodes"] = nodes;
    const Dictionary wired = connect_graph_pins(graph, pending_pin.output ? pending_pin.node : id,
            pending_pin.output ? pending_pin.pin : StringName("value"), pending_pin.output ? id : pending_pin.node,
            pending_pin.output ? StringName("value") : pending_pin.pin, false, document, script->get_graph_base());
    if (!wired.is_empty()) {
        commit(local ? "Promote to Local Variable" : "Promote to Variable", replace_graph_body(document, active_graph, wired));
        palette->hide();
        pending_pin = {};
        canvas->grab_focus();
    }
}

void VisualScriptWindow::insert_node(const Dictionary &record, bool require_connection) {
    if (script.is_null() || !record.has("kind")) {
        return;
    }
    const Dictionary document = script->get_graph();
    Dictionary next = current_graph(document);
    Array nodes = graph_array(next, "nodes").duplicate();
    const int64_t id = next_node_id(nodes);
    if (id == 0) {
        pending_pin = {};
        show_problem("This script has no more node IDs.");
        return;
    }
    Dictionary node = record.duplicate(true);
    const String kind = record.get("kind", String());
    node["id"] = id;
    node["kind"] = kind;
    node["position"] = insertion_position;
    if (kind == print_kind && !node.has("message")) {
        node["message"] = "";
    }
    nodes.append(node);
    next["nodes"] = nodes;
    bool connected = false;
    if (!pending_pin.pin.is_empty() && widgets.find(pending_pin.node) != widgets.end()) {
        const bool execution = is_execution(pending_pin.descriptor);
        const StringName base = script->get_graph_base();
        const StringName new_pin = insertion_pin(document, next, node, !pending_pin.output, pending_pin.descriptor, base,
                palette_filters_pin() && pending_pin.presentation ? &*pending_pin.presentation : nullptr);
        const int64_t source = pending_pin.output ? pending_pin.node : id;
        const int64_t destination = pending_pin.output ? id : pending_pin.node;
        const StringName source_pin = pending_pin.output ? pending_pin.pin : new_pin;
        const StringName destination_pin = pending_pin.output ? new_pin : pending_pin.pin;
        const Dictionary wired = connect_graph_pins(next, source, source_pin, destination, destination_pin, execution, document, base);
        if (!wired.is_empty()) {
            next = wired;
            connected = true;
        }
    }
    if (require_connection && !connected) {
        pending_pin = {};
        return;
    }
    commit_graph("Add Graph Node", next);
    palette->hide();
    pending_pin = {};
    canvas->grab_focus();
}

void VisualScriptWindow::insert_custom_event() {
    if (script.is_null() || active_graph != 0 || palette_filters_pin()) {
        return;
    }
    palette->hide();
    int64_t definition = 0;
    Dictionary next = panels->new_custom_event(script->get_graph(), definition);
    Array nodes = graph_array(next, "nodes").duplicate();
    const int64_t id = next_node_id(nodes);
    if (definition <= 0 || id <= 0) {
        show_problem("This script has no more event or node IDs.");
        return;
    }
    nodes.append(record_from("id", id, "kind", "custom_event", "definition", definition, "position", insertion_position));
    next["nodes"] = nodes;
    commit("Add Custom Event", next);
    pending_pin = {};
    panels->show_declaration("events", definition);
    canvas->grab_focus();
}

void VisualScriptWindow::set_message(const String &text, int64_t node_id) {
    if (script.is_null()) {
        return;
    }
    Dictionary next = current_graph(script->get_graph());
    Array nodes = graph_array(next, "nodes");
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        if (static_cast<int64_t>(node.get("id", 0)) == node_id) {
            node["message"] = text;
            nodes[index] = node;
            next["nodes"] = nodes;
            commit_graph("Edit Print Message " + String::num_int64(node_id), next, false, true);
            return;
        }
    }
}

void VisualScriptWindow::finish_move() {
    end_comment_drag();
    if (script.is_null()) {
        return;
    }
    Dictionary next = current_graph(script->get_graph());
    Array nodes = graph_array(next, "nodes");
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        const auto widget = widgets.find(static_cast<int64_t>(node.get("id", 0)));
        if (widget != widgets.end()) {
            node["position"] = widget->second->get_position_offset();
            nodes[index] = node;
        }
    }
    next["nodes"] = nodes;
    next = capture_graph_comments(next);
    if (native_node_drag) {
        native_node_drag = false;
        if (rebuild_scheduled) {
            callable_mp(this, &VisualScriptWindow::rebuild_when_idle).call_deferred();
        }
        auto *graph_canvas = static_cast<VisualScriptCanvas *>(canvas);
        graph_canvas->wire_preview.clear();
        graph_canvas->queue_redraw();
        String title;
        const Dictionary wired = node_move_wiring(next, title);
        apply_node_wire_preview(graph_array(next, "connections"), std::nullopt);
        shake_tracking = false;
        shake_pending = false;
        if (!wired.is_empty()) {
            commit_graph(title, wired);
            return;
        }
    }
    commit_graph("Move Graph Nodes", next, false);
}

PackedInt64Array VisualScriptWindow::selected_nodes() const {
    PackedInt64Array selected;
    for (const auto &[id, widget] : widgets) {
        if (widget->is_selected()) {
            selected.append(id);
        }
    }
    return selected;
}

void VisualScriptWindow::straighten_connections(const Dictionary &wire) {
    if (script.is_null()) {
        return;
    }
    // Each edge supplies the signed change in node Y required to equalize its sockets.
    struct SocketEdge {
        int64_t from;
        int64_t to;
        int32_t output;
        int32_t input;
        bool execution;
    };
    std::vector<SocketEdge> edges;
    const auto add_edge = [&](const Dictionary &connection) {
        const int64_t from = widget_id(canvas, connection.get("from_node", StringName()));
        const int64_t to = widget_id(canvas, connection.get("to_node", StringName()));
        const auto source = widgets.find(from);
        const auto target = widgets.find(to);
        const int32_t output = connection.get("from_port", -1);
        const int32_t input = connection.get("to_port", -1);
        if (source != widgets.end() && target != widgets.end() && output >= 0 && input >= 0 &&
                output < source->second->get_output_port_count() && input < target->second->get_input_port_count()) {
            edges.push_back({from, to, output, input, source->second->get_output_port_type(output) == EXECUTION});
        }
    };
    if (!wire.is_empty()) {
        add_edge(wire);
    } else {
        for (const Dictionary &connection : canvas->get_connection_list()) {
            auto *from = Object::cast_to<GraphNode>(canvas->get_node_or_null(NodePath(String(connection.get("from_node", StringName())))));
            auto *to = Object::cast_to<GraphNode>(canvas->get_node_or_null(NodePath(String(connection.get("to_node", StringName())))));
            if (from && to && from->is_selected() && to->is_selected()) {
                add_edge(connection);
            }
        }
        if (edges.empty() && selected_nodes().is_empty()) {
            const Vector2 point = canvas->get_local_mouse_position();
            if (Rect2(Vector2(), canvas->get_size()).has_point(point)) {
                add_edge(canvas->get_closest_connection_at_point(point, 8.0f));
            }
        }
    }
    if (edges.empty()) {
        return;
    }
    // A spanning forest satisfies every compatible wire. If two constraints conflict, prefer execution,
    // then a stable port order, so repeated presses cannot alternate between two layouts.
    std::sort(edges.begin(), edges.end(), [](const SocketEdge &a, const SocketEdge &b) {
        return std::make_tuple(!a.execution, a.from, a.to, a.output, a.input) <
                std::make_tuple(!b.execution, b.from, b.to, b.output, b.input);
    });
    std::map<int64_t, int64_t> parents;
    std::map<int64_t, std::vector<std::pair<int64_t, float>>> neighbors;
    const auto root = [&](int64_t id) {
        parents.try_emplace(id, id);
        while (parents.at(id) != id) {
            parents.at(id) = parents.at(parents.at(id));
            id = parents.at(id);
        }
        return id;
    };
    for (const auto &edge_to_align : edges) {
        const int64_t from_root = root(edge_to_align.from);
        const int64_t to_root = root(edge_to_align.to);
        if (from_root == to_root) {
            continue;
        }
        parents.at(to_root) = from_root;
        const float offset = widgets.at(edge_to_align.from)->get_output_port_position(edge_to_align.output).y -
                widgets.at(edge_to_align.to)->get_input_port_position(edge_to_align.input).y;
        neighbors[edge_to_align.from].emplace_back(edge_to_align.to, offset);
        neighbors[edge_to_align.to].emplace_back(edge_to_align.from, -offset);
    }
    std::vector<int64_t> anchors;
    for (const auto &[id, adjacent] : neighbors) {
        anchors.push_back(id);
    }
    std::sort(anchors.begin(), anchors.end(), [&](int64_t a, int64_t b) {
        return std::make_pair(widgets.at(a)->get_position_offset().x, a) <
                std::make_pair(widgets.at(b)->get_position_offset().x, b);
    });
    std::map<int64_t, float> heights;
    bool moved = false;
    for (const int64_t anchor : anchors) {
        if (!heights.emplace(anchor, widgets.at(anchor)->get_position_offset().y).second) {
            continue;
        }
        std::vector<int64_t> pending{anchor};
        for (size_t index = 0; index < pending.size(); ++index) {
            const int64_t from = pending[index];
            const float height = heights.at(from);
            for (const auto &[to, offset] : neighbors.at(from)) {
                if (heights.emplace(to, height + offset).second) {
                    pending.push_back(to);
                    auto *node = widgets.at(to);
                    Vector2 point = node->get_position_offset();
                    if (std::abs(point.y - (height + offset)) > 0.001f) {
                        point.y = height + offset;
                        node->set_position_offset(point);
                        moved = true;
                    }
                }
            }
        }
    }
    if (moved) {
        finish_move();
    }
}

void VisualScriptWindow::layout_selection(LayoutCommand command) {
    if (command == LayoutCommand::STRAIGHTEN) {
        straighten_connections();
        return;
    }
    const PackedInt64Array selected = selected_nodes();
    if (selected.is_empty() || (command != LayoutCommand::SNAP && selected.size() < 2)) {
        return;
    }
    std::vector<GraphNode *> picked;
    Rect2 bounds;
    for (const int64_t id : selected) {
        auto *node = widgets.at(id);
        const Rect2 area(node->get_position_offset(), node->get_size());
        bounds = picked.empty() ? area : bounds.merge(area);
        picked.push_back(node);
    }
    if (command == LayoutCommand::DISTRIBUTE_HORIZONTAL || command == LayoutCommand::DISTRIBUTE_VERTICAL) {
        const int32_t axis = command == LayoutCommand::DISTRIBUTE_HORIZONTAL ? 0 : 1;
        std::stable_sort(picked.begin(), picked.end(), [axis](const GraphNode *a, const GraphNode *b) {
            return a->get_position_offset()[axis] < b->get_position_offset()[axis];
        });
        float gap = bounds.size[axis];
        for (const auto *node : picked) gap -= node->get_size()[axis];
        gap /= static_cast<float>(picked.size() - 1);
        float offset = bounds.position[axis];
        for (auto *node : picked) {
            Vector2 point = node->get_position_offset();
            point[axis] = offset;
            node->set_position_offset(point);
            offset += node->get_size()[axis] + gap;
        }
    } else if (command == LayoutCommand::ARRANGE) {
        const Dictionary graph = current_graph(script->get_graph());
        const Array connections = graph_array(graph, "connections");
        std::map<int64_t, int32_t> columns;
        for (const int64_t id : selected) columns.emplace(id, 0);
        for (int64_t pass = 0; pass < selected.size() - 1; ++pass) {
            bool changed = false;
            for (const Dictionary &wire : connections) {
                const auto source = columns.find(static_cast<int64_t>(wire.get("from", 0)));
                const auto target = columns.find(static_cast<int64_t>(wire.get("to", 0)));
                if (source != columns.end() && target != columns.end() && target->second <= source->second &&
                        source->second < selected.size() - 1) {
                    target->second = source->second + 1;
                    changed = true;
                }
            }
            if (!changed) break;
        }
        std::map<int32_t, std::vector<GraphNode *>> lanes;
        for (const auto &[id, column] : columns) lanes[column].push_back(widgets.at(id));
        Vector2 cursor = bounds.position;
        for (const auto &[column, nodes] : lanes) {
            cursor.y = bounds.position.y;
            float width = 0;
            for (auto *node : nodes) {
                node->set_position_offset(cursor);
                cursor.y += node->get_size().y + ui::pixels(this, 32);
                width = std::max(width, node->get_size().x);
            }
            cursor.x += width + ui::pixels(this, 64);
        }
    } else {
        for (auto *node : picked) {
            Vector2 point = node->get_position_offset();
            const Vector2 size = node->get_size();
            switch (command) {
                case LayoutCommand::ALIGN_LEFT: point.x = bounds.position.x; break;
                case LayoutCommand::ALIGN_CENTER: point.x = bounds.get_center().x - size.x / 2.0f; break;
                case LayoutCommand::ALIGN_RIGHT: point.x = bounds.get_end().x - size.x; break;
                case LayoutCommand::ALIGN_TOP: point.y = bounds.position.y; break;
                case LayoutCommand::ALIGN_MIDDLE: point.y = bounds.get_center().y - size.y / 2.0f; break;
                case LayoutCommand::ALIGN_BOTTOM: point.y = bounds.get_end().y - size.y; break;
                case LayoutCommand::SNAP: point = point.snapped(Vector2(16, 16)); break;
                default: break;
            }
            node->set_position_offset(point);
        }
    }
    finish_move();
}

void VisualScriptWindow::collapse_selection(bool macro) {
    if (script.is_null()) {
        return;
    }
    String error;
    const Dictionary next = collapse_graph_selection(script->get_graph(), active_graph, selected_nodes(), macro, script->get_graph_base(), error);
    if (next.is_empty()) {
        if (!error.is_empty()) show_problem(error);
        return;
    }
    const Dictionary pruned = prune_graph_comment_members(graph_body(next, active_graph));
    commit(macro ? "Collapse to Macro" : "Collapse to Function", pruned.is_empty() ? next : replace_graph_body(next, active_graph, pruned));
    canvas->grab_focus();
}

void VisualScriptWindow::reroute_wire(Vector2 point) {
    if (script.is_null()) {
        return;
    }
    const Dictionary hovered = canvas->get_closest_connection_at_point(point, 8.0);
    const auto source = widgets.find(widget_id(canvas, hovered.get("from_node", StringName())));
    const auto target = widgets.find(widget_id(canvas, hovered.get("to_node", StringName())));
    if (source == widgets.end() || target == widgets.end()) {
        return;
    }
    const int32_t source_index = hovered.get("from_port", -1);
    const int32_t target_index = hovered.get("to_port", -1);
    const Dictionary descriptor = pin_descriptor(source->second, true, source_index);
    const StringName source_pin = pin_id(source->second, true, source_index);
    const StringName target_pin = pin_id(target->second, false, target_index);
    if (descriptor.is_empty() || source_pin.is_empty() || target_pin.is_empty()) {
        return;
    }
    Dictionary graph = current_graph(script->get_graph());
    Array nodes = graph_array(graph, "nodes");
    const int64_t id = next_node_id(nodes);
    if (id == 0) return;
    const bool execution = is_execution(descriptor);
    const Vector2 placement = (point + canvas->get_scroll_offset()) / canvas->get_zoom();
    nodes.append(record_from("id", id, "kind", "reroute", "mode", execution ? "exec" : "data", "pin", descriptor,
            "position", placement - Vector2(ui::pixels(this, ui::graph_reroute_width) * 0.5, ui::pixels(this, ui::graph_reroute_height) * 0.5)));
    Array connections = graph_array(graph, "connections");
    for (int64_t index = 0; index < connections.size(); ++index) {
        const Dictionary wire = connections[index];
        if (static_cast<int64_t>(wire.get("from", 0)) == source->first && static_cast<int64_t>(wire.get("to", 0)) == target->first &&
                StringName(wire.get("from_pin", StringName())) == source_pin && StringName(wire.get("to_pin", StringName())) == target_pin) {
            connections.remove_at(index);
            connections.append(edge(source->first, source_pin, id, execution ? "in" : "value"));
            connections.append(edge(id, execution ? "then" : "value", target->first, target_pin));
            graph["connections"] = connections;
            graph["nodes"] = nodes;
            commit_graph("Reroute Wire", graph);
            return;
        }
    }
}

void VisualScriptWindow::connect_nodes(StringName from, int64_t from_port, StringName to, int64_t to_port) {
    const auto source = widgets.find(widget_id(canvas, from));
    const auto destination = widgets.find(widget_id(canvas, to));
    if (script.is_null() || source == widgets.end() || destination == widgets.end()) {
        return;
    }
    const StringName source_pin = pin_id(source->second, true, from_port);
    const StringName destination_pin = pin_id(destination->second, false, to_port);
    if (!compatible_ports(source->second, static_cast<int32_t>(from_port), destination->second, static_cast<int32_t>(to_port))) {
        return;
    }
    const Dictionary document = script->get_graph();
    const Dictionary next = connect_graph_pins(current_graph(document), source->first, source_pin, destination->first, destination_pin,
            source->second->get_output_port_type(static_cast<int32_t>(from_port)) == EXECUTION, document, script->get_graph_base());
    if (!next.is_empty()) {
        commit_graph("Connect Graph Pins", next);
    }
}

void VisualScriptWindow::disconnect_nodes(StringName from, int64_t from_port, StringName to, int64_t to_port) {
    const auto source = widgets.find(widget_id(canvas, from));
    const auto destination = widgets.find(widget_id(canvas, to));
    if (script.is_null() || source == widgets.end() || destination == widgets.end()) {
        return;
    }
    const StringName source_pin = pin_id(source->second, true, from_port);
    const StringName destination_pin = pin_id(destination->second, false, to_port);
    Dictionary next = current_graph(script->get_graph());
    Array connections = graph_array(next, "connections");
    for (int64_t index = 0; index < connections.size(); ++index) {
        const Dictionary connection = connections[index];
        if (static_cast<int64_t>(connection.get("from", 0)) == source->first && StringName(connection.get("from_pin", "")) == source_pin &&
                static_cast<int64_t>(connection.get("to", 0)) == destination->first && StringName(connection.get("to_pin", "")) == destination_pin) {
            connections.remove_at(index);
            next["connections"] = connections;
            commit_graph("Disconnect Graph Pins", next);
            return;
        }
    }
}

Dictionary VisualScriptWindow::saved_wire(const Dictionary &wire) const {
    const int64_t from = widget_id(canvas, wire.get("from_node", StringName()));
    const int64_t to = widget_id(canvas, wire.get("to_node", StringName()));
    if (from <= 0 || to <= 0) return {};
    const auto source = widgets.find(from);
    const auto target = widgets.find(to);
    if (source == widgets.end() || target == widgets.end()) return {};
    const StringName output = pin_id(source->second, true, wire.get("from_port", -1));
    const StringName input = pin_id(target->second, false, wire.get("to_port", -1));
    return output.is_empty() || input.is_empty() ? Dictionary() : edge(from, output, to, input);
}

VisualScriptWindow::PendingPin VisualScriptWindow::pin_near(Vector2 point, int direction) const {
    PendingPin result;
    float best = static_cast<float>(ui::pixels(canvas, 15));
    const float zoom = canvas->get_zoom();
    for (int32_t child = canvas->get_child_count() - 1; child >= 0; --child) {
        auto *node = Object::cast_to<GraphNode>(canvas->get_child(child));
        if (!node || !node->is_visible() || !node->has_meta(node_id_key)) continue;
        const Rect2 card(node->get_position(), node->get_size() * zoom);
        if (!card.grow(best).has_point(point)) continue;
        const int64_t id = node->get_meta(node_id_key);
        const bool dot = node->has_meta(routing_dot_key);
        for (const bool output : {false, true}) {
            if (direction != 0 && (direction > 0) != output) continue;
            const int32_t count = output ? node->get_output_port_count() : node->get_input_port_count();
            for (int32_t index = 0; index < count; ++index) {
                const Vector2 local = output ? node->get_output_port_position(index) : node->get_input_port_position(index);
                const Vector2 anchor = node->get_position() + local * zoom;
                if (dot && (output ? point.x <= anchor.x : point.x >= anchor.x)) continue;
                const float distance = point.distance_to(anchor);
                if (distance < best) {
                    best = distance;
                    result = {id, pin_id(node, output, index), pin_descriptor(node, output, index), output};
                }
            }
        }
        // A lower card's socket cannot be clicked through the body of a higher card.
        if (card.has_point(point)) break;
    }
    return result;
}

VisualScriptWindow::PendingPin VisualScriptWindow::debug_pin_hit(Vector2 point, GraphNode *top_node) const {
    const PendingPin socket = pin_near(point);
    if (!top_node) {
        return socket;
    }
    const int64_t node = top_node->get_meta(node_id_key, 0);
    if (socket.node == node) {
        return socket;
    }
    // The label is a second precise target. Ignore literal fields and the empty space between pins.
    const Vector2 local = top_node->get_transform().affine_inverse().xform(point);
    for (const bool output : {false, true}) {
        const int32_t count = output ? top_node->get_output_port_count() : top_node->get_input_port_count();
        for (int32_t port = 0; port < count; ++port) {
            const int32_t slot = output ? top_node->get_output_port_slot(port) : top_node->get_input_port_slot(port);
            auto *row = Object::cast_to<HBoxContainer>(top_node->get_child(slot));
            if (!row || !row->get_rect().has_point(local)) {
                continue;
            }
            for (int32_t child = 0; child < row->get_child_count(); ++child) {
                const auto *label = Object::cast_to<Label>(row->get_child(child));
                if (label && label->is_visible() && label->has_meta(pin_label_direction) &&
                        bool(label->get_meta(pin_label_direction)) == output &&
                        label->get_rect().has_point(local - row->get_position())) {
                    return {node, pin_id(top_node, output, port), pin_descriptor(top_node, output, port), output};
                }
            }
        }
    }
    return {};
}

GraphNode *VisualScriptWindow::node_at_canvas_point(Vector2 point) const {
    // Godot draws a recently raised card last.
    for (int32_t index = canvas->get_child_count() - 1; index >= 0; --index) {
        auto *node = Object::cast_to<GraphNode>(canvas->get_child(index));
        if (node && node->is_visible() && Rect2(node->get_position(), node->get_size() * canvas->get_zoom()).has_point(point)) {
            return node;
        }
    }
    return nullptr;
}

void VisualScriptWindow::update_pin_hover(std::optional<Vector2> point) {
    PendingPin hit;
    if (point && !native_wire_drag && !native_node_drag && Rect2(Vector2(), canvas->get_size()).has_point(*point)) {
        auto *top = node_at_canvas_point(*point);
        hit = debug_pin_hit(*point, top);
        if (hit.node <= 0 || is_execution(hit.descriptor)) {
            if (auto *card = Object::cast_to<VisualScriptGraphNode>(top)) {
                const Vector2 local = card->get_transform().affine_inverse().xform(*point);
                if (const auto execution = card->execution_port_at(local)) {
                    const auto [port, output] = *execution;
                    hit = {int64_t(card->get_meta(node_id_key, 0)), pin_id(card, output, port), pin_descriptor(card, output, port), output};
                }
            }
        }
    }
    if (pin_hover_node && pin_hover_node != hit.node) {
        const auto old = widgets.find(pin_hover_node);
        if (old != widgets.end()) static_cast<VisualScriptGraphNode *>(old->second)->set_pin_hover({});
    }
    pin_hover_node = 0;
    const auto next = widgets.find(hit.node);
    if (next == widgets.end()) return;
    auto *widget = static_cast<VisualScriptGraphNode *>(next->second);
    const int32_t port = pin_index(widget, hit.output, hit.pin);
    if (port < 0) {
        widget->set_pin_hover({});
        return;
    }
    const int32_t slot = hit.output ? widget->get_output_port_slot(port) : widget->get_input_port_slot(port);
    widget->set_pin_hover(std::pair(slot, hit.output));
    pin_hover_node = hit.node;
}

String VisualScriptWindow::debug_canvas_tooltip(Vector2 point) const {
    if (native_wire_drag || native_node_drag || !Rect2(Vector2(), canvas->get_size()).has_point(point)) {
        return {};
    }
    const PendingPin socket = pin_near(point);
    const PendingPin hovered_pin = socket.pin.is_empty() ? debug_pin_hit(point, node_at_canvas_point(point)) : socket;
    if (hovered_pin.pin.is_empty()) return {};
    const auto widget = widgets.find(hovered_pin.node);
    if (widget == widgets.end()) return {};
    const int32_t port = pin_index(widget->second, hovered_pin.output, hovered_pin.pin);
    if (port < 0) return {};
    const int32_t slot = hovered_pin.output ? widget->second->get_output_port_slot(port) : widget->second->get_input_port_slot(port);
    const auto *row = Object::cast_to<HBoxContainer>(widget->second->get_child(slot));
    if (!row) return {};
    const String hint = row->get_meta(hovered_pin.output ? output_socket_hint : input_socket_hint, String());
    if (is_execution(hovered_pin.descriptor)) {
        return hint;
    }
    const String paused_value = debug_pin_tooltip(hovered_pin);
    return paused_value.is_empty() ? hint : paused_value + String("\n\n") + hint;
}

void VisualScriptWindow::debug_refresh_pin_labels() {
    for (const auto &[id, node] : widgets) {
        for (const bool output : {false, true}) {
            const int32_t count = output ? node->get_output_port_count() : node->get_input_port_count();
            for (int32_t port = 0; port < count; ++port) {
                const int32_t slot = output ? node->get_output_port_slot(port) : node->get_input_port_slot(port);
                auto *row = Object::cast_to<HBoxContainer>(node->get_child(slot));
                if (!row) {
                    continue;
                }
                for (int32_t child = 0; child < row->get_child_count(); ++child) {
                    auto *label = Object::cast_to<Label>(row->get_child(child));
                    if (!label || !label->has_meta(pin_label_direction) ||
                            bool(label->get_meta(pin_label_direction)) != output) {
                        continue;
                    }
                    const String paused_value = debug_pin_tooltip(
                            {id, pin_id(node, output, port), pin_descriptor(node, output, port), output});
                    const String hint = label->get_meta(pin_label_hint);
                    label->set_tooltip_text(paused_value.is_empty() ? hint : paused_value + String("\n\n") + hint);
                }
            }
        }
    }
}

void VisualScriptWindow::reset_wire_tools() {
    close_drop_choice();
    if (palette && palette->is_visible()) palette->hide();
    cancel_wire_gestures();
    interrupted_node_drag = false;
    rearm_node_move = false;
    end_comment_drag();
    inserting_on_wire = false;
    hovered_wire.clear();
    multi_connect_source = {};
    exchange_source = {};
    highlighted_wires.clear();
    swept_wires.clear();
    sweeping_wires = false;
    batch_connection_targets.clear();
    batch_connecting = false;
    native_node_drag = false;
    shake_tracking = false;
    shake_motion = {};
    shake_pending = false;
    if (canvas) {
        auto *graph_canvas = static_cast<VisualScriptCanvas *>(canvas);
        graph_canvas->set_process(false);
        graph_canvas->wire_preview.clear();
        graph_canvas->queue_redraw();
    }
}

void VisualScriptWindow::cancel_native_wire() {
    if (pointer_wire_drag) canvas->emit_signal("connection_drag_ended");
    else if (native_wire_drag) canvas->force_connection_drag_end();
}

void VisualScriptWindow::cancel_wire_gestures() {
    if (!canvas) return;
    cancel_native_wire();
    auto *graph_canvas = static_cast<VisualScriptCanvas *>(canvas);
    graph_canvas->clear_selection_gesture(true);
    if (native_node_drag) {
        // Viewport focus loss may already have queued GraphEdit's native move-finished callback.
        // Keep the new position, but require a deliberate release to change graph connectivity.
        interrupted_node_drag = true;
        rearm_node_move = false;
        shake_tracking = false;
        shake_pending = false;
        if (script.is_valid()) {
            apply_node_wire_preview(graph_array(current_graph(script->get_graph()), "connections"), std::nullopt);
        }
        callable_mp(this, &VisualScriptWindow::finish_interrupted_move).bind(node_move_generation).call_deferred();
    }
    multi_connect_source = {};
    batch_connection_targets.clear();
    batch_connecting = false;
    if (sweeping_wires) {
        swept_wires.clear();
        sweeping_wires = false;
        update_wire_highlights();
    }
    graph_canvas->set_compatible_socket({});
    graph_canvas->set_process(false);
    graph_canvas->wire_preview.clear();
    graph_canvas->queue_redraw();
}

void VisualScriptWindow::native_wire_started(const StringName &name, int64_t port, bool output) {
    native_wire_drag = true;
    const int64_t id = widget_id(canvas, name);
    const auto widget = widgets.find(id);
    if (widget != widgets.end()) {
        native_wire_source = {id, pin_id(widget->second, output, port), pin_descriptor(widget->second, output, port), output};
        if (pointer_wire_drag) {
            static_cast<VisualScriptCanvas *>(canvas)->set_dragged_socket(widget->second, port, output);
        }
    }
    wire_pan_point = canvas->get_local_mouse_position();
    canvas->set_process(true);
}

void VisualScriptWindow::native_wire_ended() {
    pointer_wire_drag = false;
    native_wire_drag = false;
    native_wire_source = {};
    static_cast<VisualScriptCanvas *>(canvas)->set_dragged_socket(nullptr, -1, false);
    if (!multi_connect_source.node && !batch_connecting) canvas->set_process(false);
}

void VisualScriptWindow::tick_wire_pan(double delta) {
    if (!native_wire_drag && !batch_connecting && multi_connect_source.node <= 0) {
        canvas->set_process(false);
        return;
    }
    const Vector2 size = canvas->get_size();
    const float margin = static_cast<float>(ui::pixels(canvas, 24));
    if (!Rect2(Vector2(), size).has_point(wire_pan_point)) return;
    const auto pressure = [margin](float value, float limit) {
        if (value < margin) return (value - margin) / margin;
        if (value > limit - margin) return (value - limit + margin) / margin;
        return 0.0f;
    };
    const Vector2 pan(pressure(wire_pan_point.x, size.x), pressure(wire_pan_point.y, size.y));
    if (pan != Vector2()) {
        canvas->set_scroll_offset(canvas->get_scroll_offset() + pan * static_cast<float>(360.0 * delta));
        if (multi_connect_source.node > 0) preview_wire_motion(wire_pan_point);
        if (pointer_wire_drag) preview_pointer_wire(wire_pan_point);
    }
}

void VisualScriptWindow::native_move_started() {
    rearm_node_move = false;
    native_node_drag = true;
    interrupted_node_drag = false;
    ++node_move_generation;
    begin_comment_drag();
    shake_tracking = !std::any_of(comment_frames.begin(), comment_frames.end(), [](const auto &entry) {
        return entry.second->is_selected() && entry.second->is_draggable();
    });
    shake_pending = false;
    clear_shake_strokes();
    // GraphEdit signals the start after the canvas sees the first motion.
    if (shake_tracking) {
        track_shake_motion(shake_motion);
    }
    queue_node_wire_preview();
}

void VisualScriptWindow::recover_node_move_start(GraphNode *card) {
    // A focus-synthetic release may reach the grabbed child without bubbling through GraphEdit.
    if (rearm_node_move && !native_node_drag && card->is_selected() && !shake_motion.is_zero_approx()) {
        native_move_started();
    }
}

void VisualScriptWindow::clear_shake_strokes() {
    shake_displacement = {};
    shake_direction = {};
    shake_extreme = {};
    shake_turns = 0;
    shake_window_start = 0;
}

void VisualScriptWindow::finish_interrupted_move(uint64_t generation) {
    if (generation == node_move_generation && native_node_drag && interrupted_node_drag) {
        finish_move();
    }
}

void VisualScriptWindow::track_shake_motion(Vector2 relative) {
    constexpr uint64_t shake_window_ms = 450;
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (shake_window_start != 0 && now - shake_window_start > shake_window_ms) {
        clear_shake_strokes();
    }
    // Use raw pointer travel, allowing a shake without leaving the card or crossing a grid step.
    const float stroke = ui::pixels(canvas, 10);
    shake_displacement += relative;
    if (shake_direction.is_zero_approx()) {
        if (shake_displacement.length_squared() >= stroke * stroke) {
            shake_direction = shake_displacement.normalized();
            shake_extreme = shake_displacement;
            shake_window_start = now;
        }
        return;
    }
    const Vector2 from_extreme = shake_displacement - shake_extreme;
    const float forward = from_extreme.dot(shake_direction);
    if (std::abs(from_extreme.cross(shake_direction)) >= stroke) {
        // Count the turn as the first stroke so a user can start shaking after an ordinary drag.
        clear_shake_strokes();
        shake_displacement = from_extreme;
        shake_direction = from_extreme.normalized();
        shake_extreme = from_extreme;
        shake_window_start = now;
    } else if (forward > 0) {
        shake_extreme = shake_displacement;
    } else if (forward <= -stroke) {
        shake_direction = -shake_direction;
        shake_extreme = shake_displacement;
        shake_pending = ++shake_turns == 2;
    }
}

bool VisualScriptWindow::try_lazy_connection(const PendingPin &origin, Vector2 point) {
    if (!native_wire_drag || origin.pin.is_empty()) return false;
    const auto source = widgets.find(origin.node);
    if (source == widgets.end()) return false;
    const int32_t port = pin_index(source->second, origin.output, origin.pin);
    if (port < 0) return false;
    float best = std::numeric_limits<float>::max();
    int64_t target_id = 0;
    int32_t target_port = -1;
    const float zoom = canvas->get_zoom();
    for (const auto &[id, target] : widgets) {
        if (origin.node == id || !Rect2(target->get_position(), target->get_size() * zoom).has_point(point)) continue;
        const int32_t count = origin.output ? target->get_input_port_count() : target->get_output_port_count();
        for (int32_t index = 0; index < count; ++index) {
            if (!(origin.output ? compatible_ports(source->second, port, target, index) : compatible_ports(target, index, source->second, port))) continue;
            const bool execution = is_execution(origin.descriptor);
            if (origin.output != execution) {
                bool occupied = false;
                for (const Dictionary &connection : canvas->get_connection_list_from_node(target->get_name())) {
                    occupied |= StringName(connection.get(origin.output ? "to_node" : "from_node", StringName())) == target->get_name() &&
                            static_cast<int64_t>(connection.get(origin.output ? "to_port" : "from_port", -1)) == index;
                }
                if (occupied) continue;
            }
            const Vector2 pin = origin.output ? target->get_input_port_position(index) : target->get_output_port_position(index);
            const float distance = (target->get_position() + pin * zoom).distance_squared_to(point);
            if (distance < best) { best = distance; target_id = id; target_port = index; }
        }
    }
    if (target_id <= 0) return false;
    const Dictionary document = script->get_graph();
    const StringName target = pin_id(widgets.at(target_id), !origin.output, target_port);
    const Dictionary next = connect_graph_pins(current_graph(document), origin.output ? origin.node : target_id,
            origin.output ? origin.pin : target, origin.output ? target_id : origin.node, origin.output ? target : origin.pin,
            is_execution(origin.descriptor), document, script->get_graph_base());
    if (next.is_empty()) return false;
    commit_graph("Connect Nearest Compatible Pin", next);
    return true;
}

bool VisualScriptWindow::run_canvas_wire_tool(const String &command) {
    auto *graph_canvas = static_cast<VisualScriptCanvas *>(canvas);
    if (command == "wire_style") {
        graph_canvas->wire_style = (graph_canvas->wire_style + 1) % 3;
        // Native connections cache their curves; rebuild applies the new geometry to drawing and hit tests.
        rebuild();
        canvas->grab_focus();
        return true;
    }
    if (command == "wire_colors") {
        neutral_wires = !neutral_wires;
        update_wire_highlights();
        canvas->grab_focus();
        return true;
    }
    if (command == "auto_insert" || command == "auto_connect" || command == "click_multi") {
        bool &setting = command == "auto_insert" ? automatic_insert : command == "auto_connect" ? automatic_connect : click_multi_connect;
        setting = !setting;
        if (!click_multi_connect && command == "click_multi") multi_connect_source = {};
        canvas->grab_focus();
        return true;
    }
    if (command != "exchange") return false;
    const PendingPin hit = pin_near(canvas->get_local_mouse_position());
    if (hit.node > 0) {
        if (!exchange_source.node || exchange_source.output != hit.output) {
            exchange_source = hit;
            for (const Dictionary &connection : canvas->get_connection_list()) {
                const Dictionary identity = saved_wire(connection);
                if (static_cast<int64_t>(identity.get(hit.output ? "from" : "to", 0)) == hit.node &&
                        StringName(identity.get(hit.output ? "from_pin" : "to_pin", StringName())) == hit.pin &&
                        !highlighted_wires.has(identity)) highlighted_wires.append(identity);
            }
            update_wire_highlights();
            return true;
        }
        const Dictionary next = exchange_pin_links(script->get_graph(), active_graph, exchange_source.node, exchange_source.pin,
                hit.node, hit.pin, hit.output, script->get_graph_base());
        exchange_source = {};
        if (!next.is_empty()) commit_graph("Exchange Wires", next);
        return true;
    }
    if (highlighted_wires.size() == 2) {
        const Dictionary first = highlighted_wires[0];
        const Dictionary second = highlighted_wires[1];
        const Dictionary next = exchange_pin_links(script->get_graph(), active_graph, first.get("to", 0),
                first.get("to_pin", StringName()), second.get("to", 0), second.get("to_pin", StringName()), false, script->get_graph_base());
        if (!next.is_empty()) commit_graph("Exchange Wires", next);
    }
    return true;
}

void VisualScriptWindow::update_wire_highlights() {
    for (const Dictionary &connection : canvas->get_connection_list()) {
        const Dictionary identity = saved_wire(connection);
        const auto source = widgets.find(identity.get("from", 0));
        const int32_t port = connection.get("from_port", -1);
        const bool data = source != widgets.end() && port >= 0 && port < source->second->get_output_port_count() &&
                source->second->get_output_port_type(port) != EXECUTION;
        canvas->set_connection_activity(connection.get("from_node", StringName()), port,
                connection.get("to_node", StringName()), connection.get("to_port", -1),
                highlighted_wires.has(identity) || swept_wires.has(identity) ? 1.0f : neutral_wires && data ? .90f : 0.0f);
    }
}

bool VisualScriptWindow::wire_tool_input(const Ref<InputEvent> &event) {
    if (script.is_null()) return false;
    const Ref<InputEventMouseButton> button = event;
    const Ref<InputEventMouseMotion> motion = event;
    if (motion.is_valid()) update_comment_drag();
    if (button.is_valid()) suppress_automatic = button->is_alt_pressed();
    if (motion.is_valid() && !native_node_drag && multi_connect_source.node > 0) {
        preview_wire_motion(motion->get_position());
    }
    if (batch_connecting && (motion.is_valid() || (button.is_valid() && button->get_button_index() == MOUSE_BUTTON_LEFT && !button->is_pressed()))) {
        const Vector2 point = motion.is_valid() ? motion->get_position() : button->get_position();
        const PendingPin target = pin_near(point, -1);
        if (target.node > 0 && target.node != multi_connect_source.node && !is_execution(target.descriptor) &&
                pins_compatible(multi_connect_source.descriptor, target.descriptor)) {
            const Dictionary item = record_from("node", target.node, "pin", target.pin);
            if (!batch_connection_targets.has(item)) batch_connection_targets.append(item);
        }
        if (button.is_valid()) {
            batch_connecting = false;
            const Dictionary document = script->get_graph();
            Dictionary next = current_graph(document);
            for (const Dictionary &item : batch_connection_targets) {
                const Dictionary updated = connect_graph_pins(next, multi_connect_source.node, multi_connect_source.pin,
                        item.get("node", 0), item.get("pin", StringName()), false, document, script->get_graph_base());
                if (!updated.is_empty()) next = updated;
            }
            batch_connection_targets.clear();
            commit_graph("Connect Multiple Data Inputs", next);
        }
        return true;
    }
    if (sweeping_wires && (motion.is_valid() || button.is_valid())) {
        const Vector2 point = motion.is_valid() ? motion->get_position() : button->get_position();
        const int samples = std::clamp(static_cast<int>(point.distance_to(sweep_point) / 5.0f) + 1, 1, 80);
        for (int index = 0; index <= samples; ++index) {
            const Dictionary identity = saved_wire(canvas->get_closest_connection_at_point(sweep_point.lerp(point, index / static_cast<float>(samples)), 6));
            if (!identity.is_empty() && !swept_wires.has(identity)) swept_wires.append(identity);
        }
        sweep_point = point;
        if (button.is_valid() && button->get_button_index() == MOUSE_BUTTON_MIDDLE && !button->is_pressed()) {
            sweeping_wires = false;
            Dictionary graph = current_graph(script->get_graph());
            Array retained;
            for (const Dictionary &connection : graph_array(graph, "connections")) {
                const Dictionary identity = edge(connection.get("from", 0), connection.get("from_pin", StringName()),
                        connection.get("to", 0), connection.get("to_pin", StringName()));
                if (!swept_wires.has(identity)) retained.append(connection);
            }
            graph["connections"] = retained;
            swept_wires.clear();
            commit_graph("Cut Wires", graph);
        }
        update_wire_highlights();
        return true;
    }
    if (button.is_null() || !button->is_pressed()) return false;
    const Vector2 point = button->get_position();
    const auto button_index = button->get_button_index();
    if (button_index == MOUSE_BUTTON_MIDDLE && button->is_alt_pressed()) {
        sweeping_wires = true;
        sweep_point = point;
        swept_wires.clear();
        return true;
    }
    const PendingPin hit = pin_near(point);
    if (button_index == MOUSE_BUTTON_LEFT && button->is_alt_pressed() && !hit.pin.is_empty()) {
        run_graph_tool(record_from("graph_tool", "break_pin", "node", hit.node, "pin", hit.pin, "output", hit.output));
        return true;
    }
    if (button_index == MOUSE_BUTTON_MIDDLE && button->is_double_click()) {
        if (!hit.pin.is_empty()) {
            highlighted_wires.clear();
            for (const Dictionary &connection : canvas->get_connection_list()) {
                const Dictionary identity = saved_wire(connection);
                if (static_cast<int64_t>(identity.get(hit.output ? "from" : "to", 0)) == hit.node &&
                        StringName(identity.get(hit.output ? "from_pin" : "to_pin", StringName())) == hit.pin) highlighted_wires.append(identity);
            }
            update_wire_highlights();
            return true;
        }
        for (const auto &[id, node] : widgets) {
            const Rect2 rect(node->get_position(), node->get_size() * canvas->get_zoom());
            if (!rect.has_point(point)) continue;
            canvas->set_selected(node);
            run_graph_tool(record_from("graph_tool", point.x < rect.get_center().x ? "upstream" : "downstream"));
            return true;
        }
    }
    if (button_index == MOUSE_BUTTON_LEFT && hit.pin.is_empty()) {
        for (const auto &[id, node] : widgets) {
            if (Rect2(node->get_position(), node->get_size() * canvas->get_zoom()).has_point(point)) return false;
        }
        const Dictionary identity = saved_wire(canvas->get_closest_connection_at_point(point, 7));
        if (!identity.is_empty()) {
            if (button->is_command_or_control_pressed()) {
                highlighted_wires.erase(identity);
            } else {
                if (!button->is_shift_pressed()) highlighted_wires.clear();
                if (!highlighted_wires.has(identity)) highlighted_wires.append(identity);
            }
            update_wire_highlights();
            return true;
        }
        if (!button->is_shift_pressed() && !button->is_command_or_control_pressed() && !highlighted_wires.is_empty()) {
            highlighted_wires.clear();
            update_wire_highlights();
        }
    }
    if (button_index == MOUSE_BUTTON_LEFT && !hit.pin.is_empty()) {
        if (click_multi_connect && hit.output && !is_execution(hit.descriptor) && !button->is_shift_pressed()) {
            multi_connect_source = hit;
            wire_pan_point = point;
            canvas->set_process(true);
            return true;
        }
        if (button->is_shift_pressed() && !is_execution(hit.descriptor)) {
            PendingPin source = hit;
            if (!source.output) {
                for (const Dictionary &connection : canvas->get_connection_list()) {
                    const Dictionary identity = saved_wire(connection);
                    if (static_cast<int64_t>(identity.get("to", 0)) == hit.node && StringName(identity.get("to_pin", StringName())) == hit.pin) {
                        const int64_t source_id = identity.get("from", 0);
                        const int32_t port = connection.get("from_port", -1);
                        source = {source_id, identity.get("from_pin", StringName()), pin_descriptor(widgets.at(source_id), true, port), true};
                        break;
                    }
                }
            }
            if (source.output) {
                multi_connect_source = source;
                batch_connecting = true;
                batch_connection_targets.clear();
                wire_pan_point = point;
                canvas->set_process(true);
                return true;
            }
        }
        if (multi_connect_source.node > 0 && !hit.output && !is_execution(hit.descriptor) &&
                pins_compatible(multi_connect_source.descriptor, hit.descriptor)) {
            const Dictionary root = script->get_graph();
            const Dictionary next = connect_graph_pins(current_graph(root), multi_connect_source.node, multi_connect_source.pin,
                    hit.node, hit.pin, false, root, script->get_graph_base());
            if (!next.is_empty()) commit_graph("Connect Data Output", next);
            return true;
        }
    }
    return false;
}

bool VisualScriptWindow::early_wire_tool_input(const Ref<InputEvent> &event) {
    if (script.is_null()) return false;
    const Ref<InputEventKey> key = event;
    if (native_node_drag && key.is_valid() && key->get_keycode() == KEY_ALT) {
        suppress_automatic = key->is_pressed();
        queue_node_wire_preview();
    }
    const Ref<InputEventMouseButton> button = event;
    if (pointer_wire_drag) return track_pointer_wire(event);
    const Ref<InputEventMouseMotion> motion = event;
    if (motion.is_valid()) {
        // Capture every left drag sample, including motion over interactive controls on another selected card.
        suppress_automatic = motion->is_alt_pressed();
        shake_motion = motion->get_button_mask().has_flag(MOUSE_BUTTON_MASK_LEFT) ? motion->get_relative() : Vector2();
        if (native_node_drag) {
            if (shake_tracking && !shake_pending && !shake_motion.is_zero_approx()) track_shake_motion(shake_motion);
            queue_node_wire_preview();
        }
    }
    if (motion.is_valid() && (batch_connecting || multi_connect_source.node > 0)) {
        wire_pan_point = motion->get_position();
    }
    if (button.is_valid() && button->get_button_index() == MOUSE_BUTTON_LEFT && interrupted_node_drag) {
        rearm_node_move = button->is_pressed() && node_at_canvas_point(button->get_position()) != nullptr &&
                !static_cast<VisualScriptCanvas *>(canvas)->interactive_control_at(button->get_position());
    }
    if (batch_connecting || sweeping_wires) return wire_tool_input(event);
    if (button.is_null() || !button->is_pressed()) return false;
    const Vector2 point = button->get_position();
    if (!Rect2(Vector2(), canvas->get_size()).has_point(point) || get_viewport()->gui_get_drag_data().get_type() != Variant::NIL ||
            palette->is_visible() || context_menu->is_visible()) return false;
    const auto index = button->get_button_index();
    if (index == MOUSE_BUTTON_RIGHT) return open_graph_context(point);
    if (index == MOUSE_BUTTON_MIDDLE && button->is_alt_pressed()) return wire_tool_input(event);
    if (index != MOUSE_BUTTON_LEFT && index != MOUSE_BUTTON_MIDDLE) return false;
    if (static_cast<VisualScriptCanvas *>(canvas)->interactive_control_at(point)) return false;
    const PendingPin hit = pin_near(point);
    if (hit.pin.is_empty()) {
        return index == MOUSE_BUTTON_LEFT && (button->is_shift_pressed() || button->is_command_or_control_pressed()) &&
                wire_tool_input(event);
    }
    if ((index == MOUSE_BUTTON_MIDDLE && button->is_double_click()) ||
            (index == MOUSE_BUTTON_LEFT && (button->is_alt_pressed() || button->is_shift_pressed() || click_multi_connect || multi_connect_source.node > 0))) {
        if (wire_tool_input(event)) return true;
    }
    if (index != MOUSE_BUTTON_LEFT || native_wire_drag) return false;
    const auto found = widgets.find(hit.node);
    if (found == widgets.end()) return false;
    const int32_t port = pin_index(found->second, hit.output, hit.pin);
    if (port < 0) return false;
    canvas->grab_focus();
    pointer_wire_drag = true;
    wire_press_point = point;
    canvas->emit_signal("connection_drag_started", found->second->get_name(), port, hit.output);
    wire_pan_point = point;
    return true;
}

VisualScriptWindow::PendingPin VisualScriptWindow::preview_pointer_wire(Vector2 point) {
    PendingPin target;
    std::optional<Vector2> snapped;
    if (Rect2(Vector2(), canvas->get_size()).has_point(point)) {
        const PendingPin hit = pin_near(point, native_wire_source.output ? -1 : 1);
        const PendingPin &source = native_wire_source.output ? native_wire_source : hit;
        const PendingPin &destination = native_wire_source.output ? hit : native_wire_source;
        if (hit.node > 0 && source.node != destination.node &&
                is_execution(source.descriptor) == is_execution(destination.descriptor) &&
                (is_execution(source.descriptor) || pins_compatible(source.descriptor, destination.descriptor))) {
            auto *widget = widgets.at(hit.node);
            const int32_t port = pin_index(widget, hit.output, hit.pin);
            if (port >= 0) {
                const Vector2 local = hit.output ? widget->get_output_port_position(port) : widget->get_input_port_position(port);
                snapped = widget->get_position() + local * canvas->get_zoom();
                target = hit;
            }
        }
    }
    static_cast<VisualScriptCanvas *>(canvas)->update_dragged_wire(point, snapped);
    return target;
}

bool VisualScriptWindow::track_pointer_wire(const Ref<InputEvent> &event) {
    if (event->is_action_pressed("ui_cancel")) {
        cancel_native_wire();
        return true;
    }
    const Ref<InputEventMouseMotion> motion = event;
    if (motion.is_valid()) {
        if (!motion->get_button_mask().has_flag(MOUSE_BUTTON_MASK_LEFT)) {
            cancel_native_wire();
            return false;
        }
        wire_pan_point = motion->get_position();
        if (motion->is_shift_pressed() && native_wire_source.output && !is_execution(native_wire_source.descriptor)) {
            multi_connect_source = native_wire_source;
            batch_connecting = true;
            batch_connection_targets.clear();
            cancel_native_wire();
            return wire_tool_input(event);
        }
        preview_pointer_wire(wire_pan_point);
        return true;
    }
    const Ref<InputEventMouseButton> button = event;
    if (button.is_null()) return false;
    if (button->is_pressed() && button->get_button_index() == MOUSE_BUTTON_RIGHT) {
        cancel_native_wire();
        return true;
    }
    if (button->is_pressed() || button->get_button_index() != MOUSE_BUTTON_LEFT) return false;
    const Vector2 point = button->get_position();
    const PendingPin origin = native_wire_source;
    const auto source = widgets.find(origin.node);
    if (source != widgets.end() && Rect2(Vector2(), canvas->get_size()).has_point(point)) {
        const PendingPin target = preview_pointer_wire(point);
        const int32_t port = pin_index(source->second, origin.output, origin.pin);
        if (point.distance_to(wire_press_point) <= ui::pixels(canvas, 7)) {
            canvas->set_selected(source->second);
        } else if (port >= 0 && target.node > 0) {
            auto *widget = widgets.at(target.node);
            const int32_t target_port = pin_index(widget, target.output, target.pin);
            if (origin.output) connect_nodes(source->second->get_name(), port, widget->get_name(), target_port);
            else connect_nodes(widget->get_name(), target_port, source->second->get_name(), port);
        } else if (port >= 0 && !try_lazy_connection(origin, point) && pin_near(point).pin.is_empty()) {
            // Stop the gesture before the popup takes focus and triggers a focus-out notification.
            cancel_native_wire();
            if (origin.output) show_output_palette(source->second->get_name(), port, point);
            else show_input_palette(source->second->get_name(), port, point);
        }
    }
    cancel_native_wire();
    return true;
}

void VisualScriptWindow::open_wire_insert() {
    if (saved_wire(hovered_wire).is_empty()) return;
    inserting_on_wire = true;
    pending_pin = {};
    open_palette(hovered_wire_point);
}

void VisualScriptWindow::insert_node_on_wire(const Dictionary &record) {
    Dictionary document = script->get_graph().duplicate();
    Dictionary graph = current_graph(document).duplicate();
    const Dictionary wire = saved_wire(hovered_wire);
    Array nodes = graph_array(graph, "nodes").duplicate();
    const int64_t id = next_node_id(nodes);
    Dictionary inserted = record.duplicate();
    inserted["id"] = id;
    inserted["position"] = insertion_position;
    const Dictionary pins = find_splice_pins(document, active_graph, wire, record, script->get_graph_base());
    palette->hide();
    inserting_on_wire = false;
    hovered_wire.clear();
    if (pins.is_empty() || id <= 0) return;
    nodes.append(inserted);
    graph["nodes"] = nodes;
    document = replace_graph_body(document, active_graph, graph);
    const Dictionary next = splice_node_into_wire(document, active_graph, wire, id, pins["input"], pins["output"], script->get_graph_base());
    if (!next.is_empty()) commit_graph("Insert Node on Wire", next);
    canvas->grab_focus();
}

Dictionary VisualScriptWindow::node_move_wiring(const Dictionary &moved_graph, String &title) const {
    if (script.is_null()) return {};
    if (shake_pending) {
        title = "Shake Node Off Wire";
        const Dictionary document = replace_graph_body(script->get_graph(), active_graph, moved_graph);
        return bypass_graph_selection(document, active_graph, selected_nodes(), false, script->get_graph_base(), true);
    }
    if (interrupted_node_drag || suppress_automatic || (!automatic_insert && !automatic_connect)) return {};
    const PackedInt64Array selected = selected_nodes();
    if (selected.size() != 1) return {};
    const int64_t id = selected[0];
    const Dictionary document = replace_graph_body(script->get_graph(), active_graph, moved_graph);
    const Dictionary graph = moved_graph;
    auto *node = widgets.at(id);
    const Rect2 card(node->get_position(), node->get_size() * canvas->get_zoom());
    const Vector2 center = card.get_center();
    if (automatic_insert) {
        const Dictionary record = find_graph_record(graph_array(graph, "nodes"), id);
        const StringName base = script->get_graph_base();
        const Rect2 target_area = card.grow(ui::pixels(canvas, 6));
        const Array saved = graph_array(graph, "connections");
        Array candidates;
        for (const Dictionary &connection : canvas->get_connections_intersecting_with_rect(target_area)) {
            const Dictionary wire = saved_wire(connection);
            if (!node_wire_preview || saved.has(wire)) candidates.append(wire);
        }
        if (node_wire_preview) {
            // Keep the currently hidden target eligible while the stock canvas shows the splice.
            for (const Dictionary &wire : saved) {
                if (node_wire_preview->has(wire)) continue;
                Vector2 from;
                Vector2 to;
                if (!canvas_pin_position(wire.get("from", 0), wire.get("from_pin", StringName()), true, from) ||
                        !canvas_pin_position(wire.get("to", 0), wire.get("to_pin", StringName()), false, to)) continue;
                const auto points = canvas->get_connection_line(from, to);
                for (int64_t index = 1; index < points.size(); ++index) {
                    if (target_area.intersects_segment(points[index - 1], points[index])) {
                        candidates.append(wire);
                        break;
                    }
                }
            }
        }
        Dictionary splice;
        for (const Dictionary &wire : candidates) {
            const Dictionary pins = find_splice_pins(document, active_graph, wire, record, base);
            if (pins.is_empty()) continue;
            const Dictionary candidate = splice_node_into_wire(document, active_graph, wire, id, pins["input"], pins["output"], base);
            if (candidate.is_empty()) continue;
            // Leave both wires alone when the card covers several valid splice targets.
            if (!splice.is_empty()) return {};
            splice = candidate;
        }
        if (!splice.is_empty()) {
            title = "Auto Insert Node";
            return splice;
        }
    }
    if (automatic_connect) {
        PackedInt64Array pair;
        float best = static_cast<float>(ui::pixels(canvas, 220));
        Dictionary next;
        for (const auto &[candidate_id, candidate] : widgets) {
            if (candidate_id == id) continue;
            const Vector2 other = candidate->get_position() + candidate->get_size() * canvas->get_zoom() * .5f;
            const float distance = center.distance_to(other);
            if (distance >= best) continue;
            pair.clear();
            pair.append(id);
            pair.append(candidate_id);
            const Dictionary result = connect_selected_graph_nodes(document, active_graph, pair, script->get_graph_base());
            if (!result.is_empty()) { best = distance; next = result; }
        }
        if (!next.is_empty()) { title = "Auto Connect Nodes"; return next; }
    }
    return {};
}

bool VisualScriptWindow::canvas_pin_position(int64_t id, const StringName &pin, bool output, Vector2 &position) const {
    const auto found = widgets.find(id);
    if (found == widgets.end()) return false;
    auto *node = found->second;
    const int32_t port = pin_index(node, output, pin);
    if (port < 0) return false;
    const Vector2 socket = output ? node->get_output_port_position(port) : node->get_input_port_position(port);
    position = node->get_position() + socket * canvas->get_zoom();
    return true;
}

void VisualScriptWindow::queue_node_wire_preview() {
    if (node_wire_preview_queued) return;
    node_wire_preview_queued = true;
    get_tree()->connect("process_frame", callable_mp(this, &VisualScriptWindow::refresh_node_wire_preview),
            Object::CONNECT_ONE_SHOT);
}

void VisualScriptWindow::refresh_node_wire_preview() {
    node_wire_preview_queued = false;
    if (!native_node_drag || script.is_null() || !canvas_ports_available()) return;
    Dictionary moved = current_graph(script->get_graph());
    Array nodes = graph_array(moved, "nodes");
    for (int64_t index = 0; index < nodes.size(); ++index) {
        Dictionary node = nodes[index];
        const auto found = widgets.find(node.get("id", 0));
        if (found != widgets.end()) node["position"] = found->second->get_position_offset();
        nodes[index] = node;
    }
    moved["nodes"] = nodes;
    String title;
    const Dictionary next = node_move_wiring(moved, title);
    const Array saved = graph_array(moved, "connections");
    const std::optional<Array> preview = next.is_empty() ? std::nullopt :
            std::optional<Array>(graph_array(next, "connections"));
    apply_node_wire_preview(saved, preview);
}

void VisualScriptWindow::apply_node_wire_preview(const Array &saved, const std::optional<Array> &preview) {
    if (node_wire_preview == preview) return;
    const Array visible = preview.value_or(saved);
    node_wire_preview = preview;
    if (canvas_wires_suspended || !canvas_ports_available()) return;
    for (const Dictionary &native : canvas->get_connection_list()) {
        const Dictionary edge = saved_wire(native);
        if (visible.has(edge)) continue;
        const StringName from = native.get("from_node", StringName());
        const StringName to = native.get("to_node", StringName());
        const int32_t output = native.get("from_port", -1);
        const int32_t input = native.get("to_port", -1);
        const auto from_widget = widgets.find(edge.get("from", 0));
        const auto to_widget = widgets.find(edge.get("to", 0));
        canvas->disconnect_node(from, output, to, input);
        if (from_widget != widgets.end() && output >= 0 && output < from_widget->second->get_output_port_count()) {
            static_cast<VisualScriptGraphNode *>(from_widget->second)->mark_connected(output, true, false);
        }
        if (to_widget != widgets.end() && input >= 0 && input < to_widget->second->get_input_port_count()) {
            static_cast<VisualScriptGraphNode *>(to_widget->second)->mark_connected(input, false, false);
        }
    }
    attach_canvas_wires(visible);
    update_wire_highlights();
}

void VisualScriptWindow::preview_wire_motion(Vector2 point) {
    auto *graph_canvas = static_cast<VisualScriptCanvas *>(canvas);
    graph_canvas->wire_preview.clear();
    Vector2 from;
    if (multi_connect_source.node && canvas_pin_position(multi_connect_source.node, multi_connect_source.pin, true, from)) {
        graph_canvas->wire_preview.emplace_back(from, point);
        for (const Dictionary &target : batch_connection_targets) {
            Vector2 to;
            if (canvas_pin_position(target.get("node", 0), target.get("pin", StringName()), false, to))
                graph_canvas->wire_preview.emplace_back(from, to);
        }
    }
    graph_canvas->queue_redraw();
}

void VisualScriptWindow::delete_selected(const TypedArray<StringName> &names) {
    if (script.is_null() || names.is_empty()) {
        return;
    }
    std::set<int64_t> removed;
    PackedInt64Array removed_comments;
    for (const StringName &name : names) {
        const int64_t node = widget_id(canvas, name);
        if (node > 0) {
            removed.insert(node);
        } else {
            for (const auto &[id, frame] : comment_frames) {
                if (frame->get_name() == name) {
                    removed_comments.append(id);
                    break;
                }
            }
        }
    }
    Dictionary next = current_graph(script->get_graph());
    const Array nodes = graph_array(next, "nodes");
    Array remaining_nodes;
    for (const Variant &item : nodes) {
        const Dictionary node = item;
        if (removed.find(static_cast<int64_t>(node.get("id", 0))) == removed.end()) {
            remaining_nodes.append(item);
        }
    }
    const Array connections = graph_array(next, "connections");
    Array remaining_connections;
    for (const Variant &item : connections) {
        const Dictionary connection = item;
        if (removed.find(static_cast<int64_t>(connection.get("from", 0))) == removed.end() &&
                removed.find(static_cast<int64_t>(connection.get("to", 0))) == removed.end()) {
            remaining_connections.append(item);
        }
    }
    next["nodes"] = remaining_nodes;
    next["connections"] = remaining_connections;
    const Dictionary without_comments = remove_graph_comments(next, removed_comments);
    if (!without_comments.is_empty()) next = without_comments;
    const Dictionary pruned = prune_graph_comment_members(next);
    commit_graph("Delete Graph Nodes", pruned.is_empty() ? next : pruned);
    canvas->grab_focus();
}


}

#endif
