// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_window.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "modules/visual_scripting/editor/visual_script_editor_style.hpp"
#include "modules/visual_scripting/editor/visual_script_organization.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/graph_edit.hpp>
#include <godot_cpp/classes/graph_element.hpp>
#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <set>

namespace crimson::visual_scripting {
namespace {
using namespace godot;
namespace ui = crimson::editor_ui;

/// Keep native frame names in a namespace separate from connectable graph nodes.
StringName frame_name(int64_t id) {
    return StringName("GraphComment_" + String::num_int64(id));
}

/// Read a stable saved frame ID or reject a malformed record.
int64_t comment_id(const Dictionary &record) {
    const Variant value = record.get("id", Variant());
    return value.get_type() == Variant::INT ? static_cast<int64_t>(value) : 0;
}

/// Keep an existing title when cancelling an in-place edit.
void close_comment_title(LineEdit *input, Label *label) {
    input->hide();
    label->show();
}

/// Open an in-place title editor without consuming the rest of the frame's drag surface.
void open_comment_title(LineEdit *input, Label *label) {
    input->set_text(label->get_text());
    label->hide();
    input->show();
    input->grab_focus();
    input->select_all();
}

/// Read a node ID only for a widget owned by the active graph canvas.
int64_t node_for_name(const std::map<int64_t, GraphNode *> &widgets, const StringName &name) {
    const String text = name;
    if (!text.begins_with("GraphNode_")) {
        return 0;
    }
    const int64_t id = text.substr(10).to_int();
    const auto found = widgets.find(id);
    return found != widgets.end() && found->second->get_name() == name ? id : 0;
}

/// Give comments enough contrast to group nodes while retaining the canvas behind them.
void style_comment(GraphElement *frame) {
    const double scale = frame->get_theme_default_base_scale();
    Color background = graph_color(GraphTone::HEADER);
    background.a = 0.56f;
    auto panel = ui::box(background, graph_color(GraphTone::DIVIDER), 0, 0, scale);
    auto selected = ui::box(background, ui::color(ui::Surface::HIGHLIGHT), 0, 0, scale);
    frame->add_theme_stylebox_override("panel", panel);
    frame->add_theme_stylebox_override("panel_selected", selected);
    const auto header = ui::box(graph_color(GraphTone::GROUP), Color(0, 0, 0, 0), 8, 4, scale);
    frame->add_theme_stylebox_override("titlebar", header);
    frame->add_theme_stylebox_override("titlebar_selected", header);
    frame->add_theme_color_override("resizer_color", graph_color(GraphTone::MUTED));
}

} // namespace

void VisualScriptWindow::rebuild_graph_comments(const Dictionary &graph) {
    std::set<int64_t> attached;
    for (const Variant &entry : graph_comments(graph)) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary record = entry;
        const int64_t id = comment_id(record);
        const Vector2 position = record.get("position", Vector2());
        const Vector2 size = record.get("size", Vector2());
        if (id <= 0 || comment_frames.count(id) != 0 || !position.is_finite() || !size.is_finite() ||
                size.x <= 0 || size.y <= 0) {
            continue;
        }
        const Variant native_frame = ClassDB::instantiate("GraphFrame");
        auto *frame = Object::cast_to<GraphElement>(native_frame.get_validated_object());
        if (!frame) {
            return;
        }
        frame->set_name(frame_name(id));
        frame->set("autoshrink_enabled", false);
        frame->set("autoshrink_margin", ui::pixels(this, 20));
        frame->set("drag_margin", ui::pixels(this, 14));
        frame->set("title", record.get("title", "Comment"));
        frame->set_position_offset(position);
        frame->set_size(size);
        frame->set_resizable(true);
        canvas->add_child(frame);
        style_comment(frame);
        frame->connect("resize_end", callable_mp(this, &VisualScriptWindow::finish_comment_resize));
        comment_frames.emplace(id, frame);

        const Variant native_titlebar = frame->call("get_titlebar_hbox");
        auto *bar = Object::cast_to<HBoxContainer>(native_titlebar.get_validated_object());
        auto *title = bar ? Object::cast_to<Label>(bar->get_child(0)) : nullptr;
        if (title) {
            title->set_clip_text(true);
            title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
            title->add_theme_color_override("font_color", graph_color(GraphTone::TEXT));
            title->add_theme_constant_override("outline_size", 0);
            auto *input = ui::text_input(bar, "", "Edit the comment title and press Enter to apply it.");
            input->set_name("GraphCommentTitle");
            input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            input->set_accessibility_name("Comment title");
            input->set_max_length(120);
            input->hide();
            input->connect("focus_exited", callable_mp_static(&close_comment_title).bind(input, title));
            input->connect("text_submitted", callable_mp(this, &VisualScriptWindow::rename_graph_comment).bind(id, input));
            auto *rename = ui::button(bar, "", "Rename this comment.", callable_mp_static(&open_comment_title).bind(input, title),
                    ui::ButtonStyle::QUIET);
            rename->set_name("RenameGraphComment");
            rename->set_accessibility_name("Rename comment");
            if (rename->has_theme_icon("Edit", "EditorIcons")) {
                rename->set_button_icon(rename->get_theme_icon("Edit", "EditorIcons"));
            } else {
                rename->set_text("Rename");
            }
        }
        for (const int64_t member : graph_comment_members(record)) {
            const auto node = widgets.find(member);
            if (node != widgets.end() && attached.insert(member).second) {
                canvas->attach_graph_element_to_frame(node->second->get_name(), frame->get_name());
            }
        }
    }
}

void VisualScriptWindow::begin_comment_drag() {
    for (const auto &[id, frame] : comment_frames) {
        if (!frame->is_selected() || !frame->is_draggable()) {
            continue;
        }
        const auto attached = canvas->get_attached_nodes_of_frame(frame->get_name());
        if (attached.is_empty()) {
            continue;
        }
        for (const StringName &name : attached) {
            const int64_t node_id = node_for_name(widgets, name);
            const auto member = widgets.find(node_id);
            if (member != widgets.end() && member->second->is_selected() && member->second->is_draggable()) {
                // GraphEdit also moves selected members on their own. Avoid expanding the frame
                // around a member's new position before its parent has moved.
                member->second->set_draggable(false);
                comment_drag_members.append(node_id);
            }
        }
        comment_drag_snap = canvas->is_snapping_enabled();
    }
    // GraphEdit snaps the frame and each member independently. A group drag preserves their offsets.
    update_comment_drag();
}

void VisualScriptWindow::update_comment_drag() {
    if (comment_drag_snap.has_value()) {
#ifdef MACOS_ENABLED
        constexpr Key snap_modifier = KEY_META;
#else
        constexpr Key snap_modifier = KEY_CTRL;
#endif
        canvas->set_snapping_enabled(Input::get_singleton()->is_key_pressed(snap_modifier));
    }
}

void VisualScriptWindow::end_comment_drag() {
    for (int64_t node_id : comment_drag_members) {
        const auto member = widgets.find(node_id);
        if (member != widgets.end()) {
            member->second->set_draggable(true);
        }
    }
    comment_drag_members.clear();
    if (comment_drag_snap.has_value()) {
        canvas->set_snapping_enabled(*comment_drag_snap);
        comment_drag_snap.reset();
    }
}

Dictionary VisualScriptWindow::capture_graph_comments(const Dictionary &graph) const {
    Array comments = graph_comments(graph).duplicate();
    bool changed = false;
    for (int64_t index = 0; index < comments.size(); ++index) {
        if (comments[index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary previous = comments[index];
        const auto found = comment_frames.find(comment_id(previous));
        if (found == comment_frames.end()) {
            continue;
        }
        auto *frame = found->second;
        std::set<int64_t> attached;
        for (const StringName &name : canvas->get_attached_nodes_of_frame(frame->get_name())) {
            const int64_t node = node_for_name(widgets, name);
            if (node > 0) {
                attached.insert(node);
            }
        }
        Array members;
        for (const int64_t member : graph_comment_members(previous)) {
            if (attached.erase(member) != 0) {
                members.append(member);
            }
        }
        for (const int64_t member : attached) {
            members.append(member);
        }
        const Vector2 position = frame->get_position_offset();
        const Vector2 size = frame->get_size();
        if (position != Vector2(previous.get("position", Vector2())) ||
                size != Vector2(previous.get("size", Vector2())) || members != Array(previous.get("members", Array()))) {
            Dictionary next = previous.duplicate();
            next["position"] = position;
            next["size"] = size;
            next["members"] = members;
            comments[index] = next;
            changed = true;
        }
    }
    if (!changed) {
        return graph;
    }
    Dictionary result = graph.duplicate();
    result["comments"] = comments;
    return result;
}

void VisualScriptWindow::finish_comment_resize(const Vector2 &) {
    if (script.is_valid() && !rebuilding_canvas) {
        const Dictionary graph = current_graph(script->get_graph());
        commit_graph("Resize Comment", capture_graph_comments(graph), false);
    }
}

void VisualScriptWindow::rename_graph_comment(const String &title, int64_t id, LineEdit *input) {
    const auto found = comment_frames.find(id);
    if (script.is_null() || rebuilding_canvas || found == comment_frames.end()) {
        return;
    }
    const String name = title.strip_edges();
    auto *frame = found->second;
    const String renamed = name.is_empty() ? String("Comment") : name;
    const Dictionary graph = current_graph(script->get_graph());
    const Rect2 bounds(frame->get_position_offset(), frame->get_size());
    commit_graph("Rename Comment", update_graph_comment(graph, id, renamed, bounds), false);
    frame->set("title", renamed);
    if (input->has_focus()) {
        canvas->grab_focus();
    }
}

void VisualScriptWindow::attach_comment_members(const TypedArray<StringName> &names, const StringName &target_name) {
    bool valid_frame = false;
    for (const auto &[id, frame] : comment_frames) {
        if (frame->get_name() == target_name) {
            valid_frame = true;
            break;
        }
    }
    if (!valid_frame) {
        return;
    }
    const auto attached = canvas->get_attached_nodes_of_frame(target_name);
    for (const StringName &name : names) {
        if (node_for_name(widgets, name) > 0 && !attached.has(name)) {
            canvas->detach_graph_element_from_frame(name);
            canvas->attach_graph_element_to_frame(name, target_name);
        }
    }
}

} // namespace crimson::visual_scripting

#endif
