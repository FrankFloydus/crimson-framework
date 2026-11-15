// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_window.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/visual_scripting/editor/visual_script_panels.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/graph_edit.hpp>
#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/style_box.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace crimson::visual_scripting {
namespace {
using namespace godot;
namespace ui = crimson::editor_ui;

/// Keep local navigation preferences outside the script and exported project.
constexpr const char *quick_jump_file = "user://crimson_visual_script_quick_jumps.dat";

/// Keep back and forward bounded across extended editing sessions.
constexpr size_t history_limit = 64;

/// Base key shared by the Find controls and graph input routing.
constexpr Key find_key = KEY_F;

/// Size the toolbar field and keep its result list compact.
constexpr int find_width = 460;
constexpr int find_row_height = 22;
constexpr int find_max_rows = 8;

/// Keep non-matching cards readable underneath the focused search.
constexpr float nonmatch_opacity = 0.32f;

/// Construct an owned control for the editor window.
template <typename T>
T *child(Node *parent, const char *name) {
    auto *control = memnew(T);
    control->set_name(name);
    parent->add_child(control);
    return control;
}

/// Read the authored array or fall back to an empty collection for older graphs.
Array records(const Dictionary &object, const StringName &key) {
    const Variant value = object.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Name built-in and reflected nodes the way they appear on the canvas.
String built_in_title(const Dictionary &node, const String &kind) {
    if (kind == "ready") {
        return "On Ready";
    }
    if (kind == "custom_script") {
        return custom_script_title(node);
    }
    if (kind == "lifecycle") {
        return "On " + String(node.get("callback", String())).trim_prefix("_").capitalize();
    }
    if (kind == "native_signal") {
        return "On " + String(node.get("signal", String())).capitalize();
    }
    if (kind == "create_node") {
        return "Create " + String(node.get("class", String()));
    }
    if (kind == "scene_node") {
        return String(NodePath(node.get("source", NodePath())));
    }
    if (kind == "operator") {
        return String(node.get("operator", String("Operator"))).capitalize();
    }
    if (kind == "switch_enum") {
        return "Switch on " + String(node.get("enum", String("Enum")));
    }
    if (kind == "call_method" || kind == "get_property" || kind == "set_property") {
        const Dictionary member = node.get("member", Dictionary());
        const String name = String(member.get("name", String())).capitalize();
        return String(kind == "get_property" ? "Get " : kind == "set_property" ? "Set " : "") + name;
    }
    return node.get("title", kind.capitalize());
}

/// Read bounded user data without decoding Godot objects.
Dictionary quick_jump_storage() {
    const Ref<FileAccess> file = FileAccess::open(quick_jump_file, FileAccess::READ);
    if (file.is_null() || file->get_length() > 2 * 1024 * 1024) {
        return {};
    }
    const Variant value = file->get_var();
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}

/// Key the shortcut by project, script and numbered slot.
String quick_jump_key(const String &script_path, int64_t slot) {
    return ProjectSettings::get_singleton()->globalize_path(script_path) + String("#") + String::num_int64(slot);
}
}

String VisualScriptWindow::graph_title(const Dictionary &document, int64_t id) {
    if (id == 0) {
        return "Event Graph";
    }
    for (const char *section : {"functions", "macros"}) {
        const Dictionary item = find_graph_record(records(document, section), id);
        if (!item.is_empty()) {
            return item.get("name", String("Graph"));
        }
    }
    return "Graph";
}

VisualScriptWindow::GraphLocation VisualScriptWindow::graph_location() const {
    const float zoom = canvas->get_zoom();
    return {active_graph, (canvas->get_scroll_offset() + canvas->get_size() * 0.5f) / zoom, zoom, selected_nodes()};
}

void VisualScriptWindow::record_location() {
    if (back_locations.size() >= history_limit) {
        back_locations.erase(back_locations.begin());
    }
    back_locations.push_back(graph_location());
    forward_locations.clear();
}

void VisualScriptWindow::reset_navigation() {
    ++camera_request;
    back_locations.clear();
    forward_locations.clear();
    close_navigation_search();
}

void VisualScriptWindow::restore_location(const GraphLocation &location) {
    if (script.is_null() || graph_body(script->get_graph(), location.graph).is_empty()) {
        return;
    }
    close_navigation_search();
    if (active_graph != location.graph) {
        reset_wire_tools();
        rebuilding_canvas = true;
        for (const auto &[id, widget] : widgets) {
            widget->set_selected(false);
        }
        active_graph = location.graph;
        rebuild();
    }
    rebuilding_canvas = true;
    for (const auto &[id, widget] : widgets) {
        widget->set_selected(location.selection.has(id));
    }
    rebuilding_canvas = false;
    canvas_selection_changed(nullptr);
    const uint64_t request = ++camera_request;
    place_location_camera(location.center, location.zoom, request);
    callable_mp(this, &VisualScriptWindow::place_location_camera).bind(location.center, location.zoom, request).call_deferred();
    canvas->grab_focus();
}

void VisualScriptWindow::place_location_camera(const Vector2 &center, float zoom, uint64_t request) {
    if (camera_request == request) {
        canvas->set_zoom(zoom);
        canvas->set_scroll_offset(center * canvas->get_zoom() - canvas->get_size() * 0.5f);
    }
}

void VisualScriptWindow::step_navigation(bool back) {
    if (script.is_null()) {
        return;
    }
    auto &source = back ? back_locations : forward_locations;
    auto &destination = back ? forward_locations : back_locations;
    while (!source.empty()) {
        const GraphLocation next = source.back();
        source.pop_back();
        if (graph_body(script->get_graph(), next.graph).is_empty()) {
            continue;
        }
        destination.push_back(graph_location());
        restore_location(next);
        break;
    }
}

void VisualScriptWindow::frame_nodes(bool selection_only) {
    Rect2 bounds;
    bool populated = false;
    const auto collect = [&](GraphElement *element) {
        if (selection_only && !element->is_selected()) {
            return;
        }
        const Rect2 extent(element->get_position_offset(), element->get_size());
        bounds = populated ? bounds.merge(extent) : extent;
        populated = true;
    };
    for (const auto &[id, widget] : widgets) {
        collect(widget);
    }
    for (const auto &[id, frame] : comment_frames) {
        collect(frame);
    }
    if (!populated) {
        return;
    }
    ++camera_request;
    const real_t padding = ui::pixels(canvas, 62);
    const Vector2 viewport = canvas->get_size();
    const Vector2 available = (viewport - Vector2(padding, padding) * 2).max(Vector2(1, 1));
    const Vector2 fit = available / bounds.size.max(Vector2(1, 1));
    const float zoom = static_cast<float>(std::min<real_t>(1.0, std::min(fit.x, fit.y)));
    canvas->set_zoom(zoom);
    canvas->set_scroll_offset(bounds.get_center() * canvas->get_zoom() - viewport * 0.5f);
    canvas->grab_focus();
}

void VisualScriptWindow::frame_opened_function(int64_t graph_id, uint64_t request) {
    if (active_graph == graph_id && camera_request == request && canvas->is_visible_in_tree()) {
        frame_nodes(false);
    }
}

bool VisualScriptWindow::navigation_shortcut(const Ref<InputEventKey> &key) {
    const auto *focused = get_viewport()->gui_get_focus_owner();
    if (key.is_null() || !key->is_pressed() || key->is_echo() || Object::cast_to<LineEdit>(focused)) {
        return false;
    }
    const Key code = key->get_keycode();
    const bool command = key->is_command_or_control_pressed();
    const bool alt = key->is_alt_pressed();
    const bool shift = key->is_shift_pressed();
    if (command && !alt && shift && code == find_key) {
        const auto symbol = selected_declaration_address();
        if (symbol.id > 0) {
            show_navigation_search(symbol);
            return true;
        }
    }
    if (command && !alt && !shift && code == find_key) {
        find_graph_nodes();
        return true;
    }
    if (alt && !command && !shift && (code == KEY_LEFT || code == KEY_RIGHT)) {
        step_navigation(code == KEY_LEFT);
        return true;
    }
    if (focused != canvas) {
        return false;
    }
    if (!alt && command != shift && code >= KEY_0 && code <= KEY_9) {
        const int64_t slot = code - KEY_0;
        if (command) {
            save_quick_jump(slot);
        } else {
            recall_quick_jump(slot);
        }
        return true;
    }
    if (!alt && !command && !shift && (code == KEY_F || code == KEY_HOME || code == KEY_A)) {
        frame_nodes(code == KEY_F);
        return true;
    }
    return false;
}

void VisualScriptWindow::build_navigation_search(Node *toolbar) {
    navigation_query = ui::search_input(toolbar, "Find nodes...", "Search node names, types and graph names in this script.");
    navigation_query->set_name("GraphFindQuery");
    navigation_query->set_accessibility_name("Find graph nodes");
    navigation_query->set_theme_type_variation("GraphPickerSearch");
    navigation_query->set_custom_minimum_size(Vector2(ui::pixels(this, 170), ui::pixels(this, 30)));
    navigation_query->set_custom_maximum_size(Vector2(ui::pixels(this, find_width), -1));
    navigation_query->set_v_size_flags(SIZE_SHRINK_CENTER);
    navigation_query->set_stretch_ratio(2.0);

    navigation_search = child<PanelContainer>(this, "GraphFindPopup");
    navigation_search->set_z_index(24);
    navigation_search->set_mouse_filter(MOUSE_FILTER_STOP);
    navigation_search->set_force_pass_scroll_events(false);
    navigation_search->add_theme_stylebox_override("panel", get_theme()->get_stylebox("panel", "CrimsonOverlayMenu"));
    navigation_search->hide();
    navigation_results = child<Tree>(navigation_search, "GraphFindResults");
    navigation_results->set_theme_type_variation("GraphPickerResults");
    navigation_results->set_hide_root(true);
    navigation_results->set_hide_folding(true);
    navigation_results->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    navigation_results->set_focus_behavior_recursive(FOCUS_BEHAVIOR_DISABLED);
    navigation_results->set_columns(2);
    navigation_results->set_column_expand_ratio(0, 3);
    navigation_results->set_column_expand_ratio(1, 2);
    navigation_results->set_column_clip_content(0, true);
    navigation_results->set_column_clip_content(1, true);
    navigation_results->set_h_scroll_enabled(false);
    navigation_results->set_select_mode(Tree::SELECT_ROW);
    navigation_results->set_allow_reselect(true);
    navigation_results->set_allow_search(false);
    navigation_results->add_theme_constant_override("draw_guides", 0);
    navigation_results->add_theme_constant_override("draw_relationship_lines", 0);
    for (const Variant &entry : navigation_results->get_children(true)) {
        if (auto *rail = Object::cast_to<Control>(entry.get_validated_object()); rail && rail->is_class("VScrollBar")) {
            rail->set_theme_type_variation("GraphPickerScrollBar");
            break;
        }
    }
    navigation_query->connect("focus_entered", callable_mp_static(+[](VisualScriptWindow *window) {
        if (!window->navigation_search->is_visible()) window->update_navigation_query(window->navigation_query->get_text());
    }).bind(this));
    navigation_query->connect("focus_exited", callable_mp_static(+[](VisualScriptWindow *window) {
        if (!window->navigation_query->has_focus()) window->close_navigation_search();
    }).bind(this), Object::CONNECT_DEFERRED);
    navigation_query->connect("item_rect_changed", callable_mp(this, &VisualScriptWindow::place_navigation_search));
    connect("resized", callable_mp(this, &VisualScriptWindow::place_navigation_search));
    navigation_query->connect("text_changed", callable_mp(this, &VisualScriptWindow::update_navigation_query));
    navigation_query->connect("gui_input", callable_mp(this, &VisualScriptWindow::navigation_search_input));
    navigation_query->connect("text_submitted", callable_mp_static(+[](const String &, VisualScriptWindow *window) {
        window->reveal_navigation_result();
    }).bind(this));
    navigation_results->connect("item_mouse_selected", callable_mp(this, &VisualScriptWindow::navigation_result_clicked));
    navigation_results->connect("item_activated", callable_mp(this, &VisualScriptWindow::reveal_navigation_result));
}

void VisualScriptWindow::find_graph_nodes() {
    show_navigation_search({});
}

void VisualScriptWindow::show_navigation_search(const SymbolAddress &symbol) {
    if (script.is_null() || (canvas_input_blocked() && !navigation_search->is_visible())) {
        return;
    }
    if (symbol.id != search_symbol.id || symbol.owner != search_symbol.owner || symbol.section != search_symbol.section) {
        navigation_query->set_text("");
    }
    search_symbol = symbol;
    navigation_query->set_placeholder(symbol.id ? "Filter declaration references..." : "Find nodes...");
    update_navigation_query(navigation_query->get_text());
    navigation_query->grab_focus();
    navigation_query->select_all();
}

void VisualScriptWindow::update_navigation_query(const String &query) {
    if (script.is_null() || !navigation_query->is_visible_in_tree() ||
            (canvas_input_blocked() && !navigation_search->is_visible())) {
        return;
    }
    if (!navigation_search->is_visible()) {
        navigation_search->move_to_front();
        navigation_search->show();
    }
    refresh_navigation_search(query);
}

void VisualScriptWindow::place_navigation_search() {
    if (!navigation_search->is_visible()) return;
    const Rect2 field = navigation_query->get_global_rect();
    const Vector2 anchor = get_global_transform().affine_inverse().xform(field.position + Vector2(0, field.size.y)) +
            Vector2(0, ui::pixels(this, ui::spacing::xs));
    TreeItem *root = navigation_results->get_root();
    const int visible_rows = root ? std::clamp(root->get_child_count(), 1, find_max_rows) : 1;
    const real_t room = std::max<real_t>(1, get_size().y - anchor.y - ui::pixels(this, ui::spacing::sm));
    const real_t desired = ui::pixels(this, visible_rows * find_row_height + 2 * ui::spacing::sm);
    navigation_search->set_position(anchor);
    navigation_search->set_size(Vector2(navigation_query->get_size().x, std::min(room, desired)));
}

void VisualScriptWindow::navigation_search_input(const Ref<InputEvent> &event) {
    if (!navigation_search->is_visible()) return;
    const bool down = event->is_action_pressed("ui_down", true, true);
    if (!down && !event->is_action_pressed("ui_up", true, true)) return;
    TreeItem *item = navigation_results->get_selected();
    while (item) {
        item = down ? item->get_next_visible() : item->get_prev_visible();
        if (item && item->is_selectable(0)) {
            item->select(0);
            navigation_results->scroll_to_item(item);
            break;
        }
    }
    navigation_query->accept_event();
}

bool VisualScriptWindow::dismiss_navigation_input(const Ref<InputEvent> &event) {
    if (!navigation_search || !navigation_search->is_visible()) return false;
    if (event->is_action_pressed("ui_cancel")) {
        close_navigation_search();
        canvas->grab_focus();
        return true;
    }
    const Ref<InputEventMouseButton> button = event;
    if (button.is_valid() && button->is_pressed() && (button->get_button_index() == MOUSE_BUTTON_LEFT ||
                button->get_button_index() == MOUSE_BUTTON_RIGHT || button->get_button_index() == MOUSE_BUTTON_MIDDLE) &&
            !navigation_query->get_global_rect().has_point(button->get_position()) &&
            !navigation_search->get_global_rect().has_point(button->get_position())) {
        close_navigation_search();
    }
    return false;
}

void VisualScriptWindow::navigation_result_clicked(Vector2, int64_t button) {
    if (button == MOUSE_BUTTON_LEFT) {
        // Opening another graph can rebuild controls after Godot's Tree finishes dispatching this event.
        callable_mp(this, &VisualScriptWindow::reveal_navigation_result).call_deferred();
    }
}

VisualScriptWindow::SymbolAddress VisualScriptWindow::declaration_address(const Dictionary &node, int64_t owner) const {
    const String kind = node.get("kind", String());
    if (kind == "get_variable" || kind == "set_variable") {
        const bool local = String(node.get("scope", String())) == "local";
        return {local ? StringName("locals") : StringName("variables"), node.get("variable", 0), local ? owner : 0};
    }
    const int64_t definition = node.get("definition", 0);
    if (kind == "call_function" || kind == "function_entry" || kind == "return") {
        return {"functions", definition > 0 ? definition : owner, 0};
    }
    if (kind == "call_macro" || kind == "macro_entry" || kind == "macro_exit") {
        return {"macros", definition > 0 ? definition : owner, 0};
    }
    if (kind == "custom_event" || kind == "emit_event") {
        return {"events", definition, 0};
    }
    return {};
}

VisualScriptWindow::SymbolAddress VisualScriptWindow::selected_declaration_address() const {
    if (script.is_null()) {
        return {};
    }
    const Dictionary sidebar_symbol = panels->selected_symbol();
    if (!sidebar_symbol.is_empty()) {
        return {sidebar_symbol.get("section", StringName()), sidebar_symbol.get("id", 0), sidebar_symbol.get("owner", 0)};
    }
    const PackedInt64Array selection = selected_nodes();
    if (selection.size() != 1) {
        return {};
    }
    return declaration_address(find_graph_record(records(current_graph(script->get_graph()), "nodes"), selection[0]), active_graph);
}

bool VisualScriptWindow::has_selected_declaration() const {
    return selected_declaration_address().id > 0;
}

void VisualScriptWindow::refresh_navigation_search(const String &query) {
    if (script.is_null()) {
        return;
    }
    navigation_results->clear();
    TreeItem *root = navigation_results->create_item();
    TreeItem *first = nullptr;
    const Dictionary document = script->get_graph();
    const String needle = query.strip_edges();
    std::unordered_set<int64_t> matching_nodes;
    PackedInt64Array graphs;
    graphs.append(active_graph);
    if (active_graph != 0) {
        graphs.append(0);
    }
    for (const char *section : {"functions", "macros"}) {
        for (const Dictionary &definition : records(document, section)) {
            const int64_t id = definition.get("id", 0);
            if (id > 0 && id != active_graph) {
                graphs.append(id);
            }
        }
    }
    for (int64_t graph_id : graphs) {
        const Dictionary body = graph_body(document, graph_id);
        const String title = graph_title(document, graph_id);
        TreeItem *category = nullptr;
        for (const Dictionary &node : records(body, "nodes")) {
            const String kind = node.get("kind", String());
            const int64_t id = node.get("id", 0);
            const auto symbol = declaration_address(node, graph_id);
            if (search_symbol.id > 0 && (symbol.id != search_symbol.id || symbol.section != search_symbol.section ||
                    symbol.owner != search_symbol.owner)) {
                continue;
            }
            String name = built_in_title(node, kind);
            if (symbol.id > 0) {
                const Dictionary source = symbol.owner > 0 ? graph_body(document, symbol.owner) : document;
                const Dictionary declaration = find_graph_record(records(source, symbol.section), symbol.id);
                const String symbol_name = declaration.get("name", String());
                if (!symbol_name.is_empty()) {
                    const String prefix = kind == "get_variable" ? "Get " : kind == "set_variable" ? "Set " :
                            kind == "custom_event" ? "On " : kind == "emit_event" ? "Emit " : "";
                    name = prefix + symbol_name;
                }
            }
            if (graph_id == active_graph) {
                const auto widget = widgets.find(id);
                if (widget != widgets.end() && !widget->second->get_title().is_empty()) {
                    name = widget->second->get_title();
                }
            }
            const String searchable = name + String(" ") + kind.capitalize() + String(" ") + title;
            if (id <= 0 || (!needle.is_empty() && searchable.findn(needle) < 0)) {
                continue;
            }
            if (graph_id == active_graph) {
                matching_nodes.insert(id);
            }
            if (!category && needle.is_empty()) {
                category = navigation_results->create_item(root);
                category->set_text(0, title);
                category->set_selectable(0, false);
                category->set_selectable(1, false);
                category->set_custom_color(0, ui::color(ui::Surface::MUTED));
                category->set_custom_font_size(0, ui::pixels(this, 12));
                category->set_custom_font(0, get_theme()->get_font("font", "Label"));
            }
            TreeItem *result = navigation_results->create_item(root);
            result->set_text(0, name);
            result->set_text_overrun_behavior(0, TextServer::OVERRUN_TRIM_ELLIPSIS);
            if (!needle.is_empty()) {
                result->set_text(1, title);
                result->set_custom_color(1, ui::color(ui::Surface::MUTED));
                result->set_custom_font_size(1, ui::pixels(this, 12));
                result->set_text_alignment(1, HORIZONTAL_ALIGNMENT_RIGHT);
                result->set_text_overrun_behavior(1, TextServer::OVERRUN_TRIM_ELLIPSIS);
            }
            const auto icon = ui::icon(navigation_results, "search");
            if (icon.is_valid()) {
                result->set_icon(0, icon);
                result->set_icon_max_width(0, ui::pixels(this, 16));
            }
            result->set_tooltip_text(0, title);
            Dictionary address;
            address["graph"] = graph_id;
            address["node"] = id;
            result->set_metadata(0, address);
            if (!first) {
                first = result;
            }
        }
    }
    if (first) {
        first->select(0);
    } else {
        TreeItem *empty = navigation_results->create_item(root);
        empty->set_text(0, search_symbol.id ? "No references found" : "No matching nodes");
        empty->set_selectable(0, false);
        empty->set_selectable(1, false);
        empty->set_custom_color(0, ui::color(ui::Surface::MUTED));
    }
    restore_navigation_card_colors();
    if (navigation_search->is_visible() && !needle.is_empty()) {
        for (const auto &[id, widget] : widgets) {
            if (!matching_nodes.count(id)) {
                const Color original = widget->get_modulate();
                navigation_dimmed_cards.emplace(widget->get_instance_id(), original);
                Color dimmed = original;
                dimmed.a *= nonmatch_opacity;
                widget->set_modulate(dimmed);
            }
        }
    }
    place_navigation_search();
}

void VisualScriptWindow::restore_navigation_card_colors() {
    for (const auto &[id, color] : navigation_dimmed_cards) {
        if (auto *card = Object::cast_to<GraphNode>(ObjectDB::get_instance(id))) {
            card->set_modulate(color);
        }
    }
    navigation_dimmed_cards.clear();
}

void VisualScriptWindow::close_navigation_search() {
    if (!navigation_search || !navigation_search->is_visible()) return;
    navigation_search->hide();
    restore_navigation_card_colors();
    search_symbol = {};
    navigation_query->set_text("");
    navigation_query->set_placeholder("Find nodes...");
}

void VisualScriptWindow::reveal_navigation_result() {
    if (!navigation_search->is_visible()) {
        return;
    }
    const TreeItem *item = navigation_results->get_selected();
    if (!item || item->get_metadata(0).get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary result = item->get_metadata(0);
    const int64_t target_graph = result.get("graph", 0);
    const int64_t target_node = result.get("node", 0);
    if (script.is_null() || find_graph_record(records(graph_body(script->get_graph(), target_graph), "nodes"), target_node).is_empty()) {
        refresh_navigation_search(navigation_query->get_text());
        return;
    }
    close_navigation_search();
    record_location();
    GraphLocation location = graph_location();
    location.graph = target_graph;
    location.selection.clear();
    location.selection.append(target_node);
    restore_location(location);
    // Native containers settle their desired sizes after a function or macro view has rebuilt.
    callable_mp(this, &VisualScriptWindow::frame_nodes).bind(true).call_deferred();
}

void VisualScriptWindow::go_to_definition(int64_t node_id) {
    const Dictionary document = script->get_graph();
    const Dictionary node = find_graph_record(records(current_graph(document), "nodes"), node_id);
    const auto symbol = declaration_address(node, active_graph);
    if (symbol.id == 0) {
        return;
    }
    if (symbol.section == StringName("functions") || symbol.section == StringName("macros")) {
        open_graph(symbol.id);
    } else if (symbol.section == StringName("events") && String(node.get("kind", String())) == "emit_event") {
        const Array nodes = records(document, "nodes");
        for (const Dictionary &event_node : nodes) {
            if (String(event_node.get("kind", String())) == "custom_event" &&
                    static_cast<int64_t>(event_node.get("definition", 0)) == symbol.id) {
                GraphLocation target = graph_location();
                target.graph = 0;
                target.selection.clear();
                target.selection.append(event_node.get("id", 0));
                record_location();
                restore_location(target);
                callable_mp(this, &VisualScriptWindow::frame_nodes).bind(true).call_deferred();
                break;
            }
        }
    }
    panels->show_declaration(symbol.section, symbol.id);
}

void VisualScriptWindow::navigation_node_input(const Ref<InputEvent> &event, int64_t node_id) {
    const Ref<InputEventMouseButton> mouse = event;
    if (script.is_valid() && mouse.is_valid() && mouse->get_button_index() == MOUSE_BUTTON_LEFT &&
            mouse->is_pressed() && mouse->is_double_click()) {
        const Dictionary node = find_graph_record(records(current_graph(script->get_graph()), "nodes"), node_id);
        if (declaration_address(node, active_graph).id > 0) {
            // Changing graphs can free the sender. Let Godot finish dispatching the pointer event first.
            callable_mp(this, &VisualScriptWindow::go_to_definition).bind(node_id).call_deferred();
        }
    }
}

bool VisualScriptWindow::navigation_palette_action(const Dictionary &metadata) {
    const String action = metadata.get("navigation", String());
    if (action.is_empty()) {
        return false;
    }
    if (action == "event_graph") {
        open_graph(0);
    } else if (action == "find") {
        find_graph_nodes();
    } else if (action == "definition") {
        const PackedInt64Array selection = selected_nodes();
        if (selection.size() == 1) {
            go_to_definition(selection[0]);
        }
    } else if (action == "references") {
        const auto symbol = selected_declaration_address();
        if (symbol.id > 0) {
            show_navigation_search(symbol);
        }
    } else if (action == "frame_selection" || action == "frame_graph") {
        frame_nodes(action == "frame_selection");
    }
    return true;
}

void VisualScriptWindow::save_quick_jump(int64_t slot) {
    if (script.is_null() || !script->get_path().begins_with("res://")) {
        return;
    }
    const GraphLocation location = graph_location();
    Dictionary saved = quick_jump_storage();
    Dictionary view;
    view["graph"] = location.graph;
    view["center"] = location.center;
    view["zoom"] = location.zoom;
    saved[quick_jump_key(script->get_path(), slot)] = view;
    const Ref<FileAccess> file = FileAccess::open(quick_jump_file, FileAccess::WRITE);
    if (file.is_valid()) {
        file->store_var(saved);
    }
}

void VisualScriptWindow::recall_quick_jump(int64_t slot) {
    if (script.is_null() || !script->get_path().begins_with("res://")) {
        return;
    }
    const Variant saved = quick_jump_storage().get(quick_jump_key(script->get_path(), slot), Variant());
    if (saved.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary value = saved;
    const int64_t graph = value.get("graph", -1);
    const Vector2 center = value.get("center", Vector2());
    const float zoom = value.get("zoom", 1.0f);
    if (graph < 0 || graph_body(script->get_graph(), graph).is_empty() || !center.is_finite() || !std::isfinite(zoom) || zoom <= 0.0f) {
        return;
    }
    record_location();
    restore_location({graph, center, zoom, {}});
}

} // namespace crimson::visual_scripting

#endif
