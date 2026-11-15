// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_window.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/visual_scripting/editor/visual_script_clipboard.hpp"
#include "modules/visual_scripting/editor/visual_script_organization.hpp"
#include "modules/visual_scripting/editor/visual_script_wire_tools.hpp"

#include <godot_cpp/classes/button_group.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/graph_edit.hpp>
#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

#include <algorithm>
#include <utility>

namespace crimson::visual_scripting {

using namespace godot;
namespace ui = crimson::editor_ui;

void VisualScriptWindow::copy_selected_nodes() {
    if (script.is_null() || selected_nodes().is_empty()) return;
    String error;
    const String encoded = encode_graph_clipboard(script->get_graph(), active_graph, script->get_instance_id(), selected_nodes(), error);
    if (encoded.is_empty()) {
        if (!error.is_empty()) show_problem(error);
        return;
    }
    graph_clipboard = encoded;
    DisplayServer *display = DisplayServer::get_singleton();
    if (display->has_feature(DisplayServer::FEATURE_CLIPBOARD)) display->clipboard_set(encoded);
}

void VisualScriptWindow::cut_selected_nodes() {
    const auto selected = selected_nodes();
    if (script.is_null() || selected.is_empty()) return;
    const Dictionary document = script->get_graph();
    String error;
    const String encoded = encode_graph_clipboard(document, active_graph, script->get_instance_id(), selected, error);
    if (encoded.is_empty()) {
        if (!error.is_empty()) show_problem(error);
        return;
    }
    const Dictionary next = cut_graph_selection(document, active_graph, selected, error);
    if (next.is_empty()) {
        if (!error.is_empty()) show_problem(error);
        return;
    }
    graph_clipboard = encoded;
    DisplayServer *display = DisplayServer::get_singleton();
    if (display->has_feature(DisplayServer::FEATURE_CLIPBOARD)) display->clipboard_set(encoded);
    commit("Cut Graph Nodes", next);
}

void VisualScriptWindow::paste_nodes() {
    if (script.is_null() || active_modal()) return;
    DisplayServer *display = DisplayServer::get_singleton();
    const String encoded = display->has_feature(DisplayServer::FEATURE_CLIPBOARD) ? display->clipboard_get() : graph_clipboard;
    Vector2 point = canvas->get_local_mouse_position();
    if (!Rect2(Vector2(), canvas->get_size()).has_point(point)) point = canvas->get_size() * .5f;
    const Vector2 anchor = ((point + canvas->get_scroll_offset()) / canvas->get_zoom()).snapped(Vector2(16, 16));
    if (is_visual_script_document_clipboard(encoded)) {
        paste_document(encoded, anchor);
        return;
    }
    String error;
    PackedInt64Array created;
    const Dictionary next = paste_graph_clipboard(script->get_graph(), active_graph, script->get_instance_id(), encoded, anchor,
            created, error, script->get_graph_base());
    if (!next.is_empty()) {
        pending_canvas_selection = created;
        commit("Paste Graph Nodes", next);
    } else if (!error.is_empty()) {
        show_problem(error);
    }
}

void VisualScriptWindow::paste_document(const String &encoded, Vector2 anchor) {
    if (active_graph != 0) {
        show_problem("Open the Event Graph before pasting this whole Visual Script.");
        return;
    }

    PendingDocumentPaste paste;
    paste.owner = script;
    paste.graph = active_graph;
    paste.base = script->get_graph_base();
    paste.anchor = anchor;
    paste.destination = script->get_graph();
    String error;
    paste.source = decode_visual_script_document(encoded, paste.base, error);
    if (paste.source.is_empty()) {
        show_problem(error.is_empty() ? String("This Visual Script could not be read from the clipboard.") : error);
        return;
    }
    const Callable allow_export = callable_mp(script.ptr(), &VisualScript::can_export_for_owners);
    paste.conflicts = visual_script_document_conflicts(paste.destination, paste.source, paste.base, error, allow_export);
    if (!error.is_empty()) {
        show_problem(error);
        return;
    }
    if (!paste.conflicts.is_empty()) {
        review_document_paste(std::move(paste));
        return;
    }
    paste.result = merge_visual_script_document(paste.destination, paste.source, paste.base, paste.anchor, paste.choices,
            paste.created, error, &paste.converted_lifecycle, allow_export);
    if (paste.result.is_empty()) {
        show_problem(error.is_empty() ? String("This Visual Script could not be pasted.") : error);
        return;
    }
    finish_document_paste(paste);
}

/// Keep a conflict row's full caption available without imposing the width of an authored name on the dialog.
static void fit_paste_conflict_label(Label *label, const String &caption) {
    label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    label->set_tooltip_text(caption);
}

void VisualScriptWindow::review_document_paste(PendingDocumentPaste paste) {
    prepare_modal();
    paste.choices.resize(paste.conflicts.size());
    pending_document_paste = std::move(paste);
    const Array &conflicts = pending_document_paste->conflicts;
    auto *scroll = memnew(ScrollContainer);
    scroll->set_name("PasteConflictList");
    scroll->set_accessibility_name("Conflicting declarations");
    scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    scroll->set_follow_focus(true);
    document_paste_dialog->body()->add_child(scroll);
    auto *list = memnew(VBoxContainer);
    list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    scroll->add_child(list);
    list->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::lg));

    for (int64_t index = 0; index < conflicts.size(); ++index) {
        const Dictionary conflict = conflicts[index];
        const String kind = conflict.get("kind_label", String("Declaration"));
        const String name = conflict.get("name", String());
        const String copy_name = conflict.get("copy_name", String());
        const String reason = conflict.get("reason", String());
        const bool replace = conflict.get("replace_supported", false);
        const bool keep_both = conflict.get("copy_supported", false);
        if (keep_both) pending_document_paste->choices.set(index, "keep_both");

        auto *row = memnew(VBoxContainer);
        row->set_name(String("PasteConflict") + String::num_int64(index + 1));
        row->set_meta("section", conflict.get("section", StringName()));
        row->set_meta("declaration_name", name);
        row->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::xxs));
        list->add_child(row);
        const String caption = kind + String(" \"") + name + String("\"");
        fit_paste_conflict_label(ui::label(row, caption, ui::LabelStyle::HEADING), caption);

        auto *actions = memnew(HBoxContainer);
        actions->add_theme_constant_override("separation", ui::pixels(this, ui::spacing::lg));
        row->add_child(actions);
        Ref<ButtonGroup> group;
        group.instantiate();
        const Callable choose = callable_mp(this, &VisualScriptWindow::choose_document_paste_conflict);
        auto *replace_action = ui::radio(actions, "Replace", group, false, choose.bind(index, String("replace")), !replace);
        replace_action->set_name("Replace");
        replace_action->set_accessibility_name(String("Replace ") + name);
        replace_action->set_tooltip_text(replace ? String("Use the incoming declaration for this name.") : reason);
        auto *copy_action = ui::radio(actions, "Keep Both", group, keep_both, choose.bind(index, String("keep_both")), !keep_both);
        copy_action->set_name("KeepBoth");
        copy_action->set_accessibility_name(String("Keep both declarations named ") + name);
        copy_action->set_tooltip_text(keep_both ? String("Add the incoming declaration as ") + copy_name + String(".") : reason);

        if (keep_both && !copy_name.is_empty()) {
            const String detail = String("Keep Both adds ") + copy_name;
            fit_paste_conflict_label(ui::label(row, detail, ui::LabelStyle::MUTED), detail);
        }
        if (!reason.is_empty()) {
            fit_paste_conflict_label(ui::label(row, reason, ui::LabelStyle::MUTED), reason);
        }
    }

    const real_t height = std::min<real_t>(list->get_combined_minimum_size().y, ui::pixels(this, 220));
    scroll->set_custom_minimum_size(Vector2(0, height));
    document_paste_dialog->set_confirm_enabled(!pending_document_paste->choices.has(String()));
    document_paste_dialog->open("Resolve Paste Conflicts",
            "These names already exist in this Visual Script. Choose which declarations to replace or keep alongside a renamed copy.");
}

void VisualScriptWindow::choose_document_paste_conflict(bool selected, int64_t index, const String &choice) {
    if (!selected || !pending_document_paste || pending_document_paste->stale) return;
    auto &choices = pending_document_paste->choices;
    choices.set(index, choice);
    document_paste_dialog->set_confirm_enabled(!choices.has(String()));
    document_paste_dialog->set_validation_message({});
}

bool VisualScriptWindow::validate_document_paste() {
    if (!pending_document_paste) return false;
    auto &paste = *pending_document_paste;
    if (paste.stale || script.is_null() || script != paste.owner || active_graph != paste.graph ||
            script->get_graph_base() != paste.base || script->get_graph() != paste.destination) {
        paste.stale = true;
        document_paste_dialog->set_validation_message("The graph changed while this prompt was open. Cancel and paste again.");
        return false;
    }

    String error;
    const Callable allow_export = callable_mp(script.ptr(), &VisualScript::can_export_for_owners);
    const Array current_conflicts = visual_script_document_conflicts(paste.destination, paste.source, paste.base, error, allow_export);
    if (!error.is_empty() || current_conflicts != paste.conflicts) {
        paste.stale = true;
        document_paste_dialog->set_validation_message("The available paste choices changed while this prompt was open. Cancel and paste again.");
        return false;
    }

    paste.result = merge_visual_script_document(paste.destination, paste.source, paste.base, paste.anchor, paste.choices,
            paste.created, error, &paste.converted_lifecycle, allow_export);
    if (!paste.result.is_empty()) return true;
    document_paste_dialog->set_validation_message(error.is_empty() ? String("This Visual Script could not be pasted.") : error);
    return false;
}

void VisualScriptWindow::confirm_document_paste() {
    const PendingDocumentPaste paste = std::move(*pending_document_paste);
    clear_document_paste();
    finish_document_paste(paste);
}

void VisualScriptWindow::clear_document_paste() {
    pending_document_paste.reset();
    auto *body = document_paste_dialog->body();
    if (body->get_child_count() == 0) return;
    auto *list = body->get_child(0);
    body->remove_child(list);
    list->queue_free();
}

/// Explain automatic conversion through Godot's nonmodal editor notifications.
static void notify_converted_callbacks(int64_t count) {
    if (count <= 0) return;
    auto *editor = EditorInterface::get_singleton();
    // EditorToaster is part of stock Godot but is not in Crimson's generated class list.
    if (!editor || !editor->has_method("get_editor_toaster")) return;
    auto *toaster = editor->call("get_editor_toaster").get_validated_object();
    if (toaster) {
        const String message = count == 1 ? "Pasted a duplicate callback as a Custom Event. Call it explicitly to run it." :
                "Pasted duplicate callbacks as Custom Events. Call them explicitly to run them.";
        toaster->call("push_toast", message);
    }
}

void VisualScriptWindow::finish_document_paste(const PendingDocumentPaste &paste) {
    canvas->grab_focus();
    if (paste.result == paste.destination) return;
    pending_canvas_selection = paste.created;
    commit("Paste Visual Script", paste.result);
    notify_converted_callbacks(paste.converted_lifecycle);
    if (Array(paste.destination.get("nodes", Array())).is_empty() && graph_comments(paste.destination).is_empty()) {
        callable_mp(this, &VisualScriptWindow::frame_opened_function).bind(active_graph, camera_request).call_deferred();
    }
}

void VisualScriptWindow::duplicate_selected_nodes(bool keep_inputs) {
    const auto selected = selected_nodes();
    if (script.is_null() || selected.is_empty()) return;
    String error;
    PackedInt64Array created;
    const Dictionary next = duplicate_graph_selection(script->get_graph(), active_graph, selected, Vector2(40, 40), keep_inputs,
            created, error, script->get_graph_base());
    if (!next.is_empty()) {
        pending_canvas_selection = created;
        commit(keep_inputs ? "Duplicate Nodes with Inputs" : "Duplicate Graph Nodes", next);
    } else if (!error.is_empty()) {
        show_problem(error);
    }
}

bool VisualScriptWindow::graph_tool_shortcut(const Ref<InputEventKey> &key) {
    const bool command = key->is_command_or_control_pressed();
    const bool alt = key->is_alt_pressed();
    const bool shift = key->is_shift_pressed();
    const auto code = key->get_keycode();
    if (!command && !alt && !shift && code == KEY_ESCAPE) {
        if (native_wire_drag || multi_connect_source.node || exchange_source.node || sweeping_wires) {
            reset_wire_tools();
            update_wire_highlights();
            return true;
        }
    }
    const char *tool = nullptr;
    if (!alt && !command && !shift && code == KEY_C) tool = "comment";
    if (alt && !command) {
        if (!shift && code == KEY_A) tool = "upstream";
        if (!shift && code == KEY_D) tool = "downstream";
        if (!shift && code == KEY_V) tool = "duplicate_inputs";
        if (!shift && code == KEY_C) tool = "connect_selected";
        if (!shift && code == KEY_T) tool = "exchange";
        if (!shift && code == KEY_R) {
            layout_selection(LayoutCommand::ARRANGE);
            return true;
        }
        if (code == KEY_X) tool = shift ? "bypass" : "reconnect";
    }
    if (!tool) return false;
    Dictionary action;
    action["graph_tool"] = tool;
    run_graph_tool(action);
    return true;
}

void VisualScriptWindow::run_graph_tool(const Dictionary &command) {
    if (script.is_null()) return;
    const String tool = command.get("graph_tool", String());
    if (run_canvas_wire_tool(tool)) return;
    if (tool == "copy") { copy_selected_nodes(); return; }
    if (tool == "cut") { cut_selected_nodes(); return; }
    if (tool == "paste") { paste_nodes(); return; }
    if (tool == "duplicate" || tool == "duplicate_inputs") { duplicate_selected_nodes(tool == "duplicate_inputs"); return; }
    const Dictionary document = script->get_graph();
    const Dictionary graph = current_graph(document);
    const auto selected = selected_nodes();
    if (tool == "upstream" || tool == "downstream") {
        const PackedInt64Array expanded = select_graph_dependencies(graph, selected, tool == "upstream");
        for (const auto &[id, node] : widgets) node->set_selected(expanded.has(id));
        canvas->grab_focus();
        return;
    }
    Dictionary next;
    String title;
    if (tool == "comment") {
        Rect2 bounds(insertion_position, Vector2(240, 120));
        bool first = true;
        for (const int64_t id : selected) {
            auto *node = widgets.at(id);
            const Rect2 region(node->get_position_offset(), node->get_size());
            bounds = first ? region : bounds.merge(region);
            first = false;
        }
        if (!first) {
            bounds = bounds.grow(26);
            bounds.position.y -= 18;
            bounds.size.y += 18;
        }
        next = add_graph_comment(graph, selected, bounds);
        title = "Add Graph Comment";
    } else if (tool == "break_nodes") {
        next = break_node_links(graph, selected);
        title = "Break All Node Links";
    } else if (tool == "connect_selected") {
        next = connect_selected_graph_nodes(document, active_graph, selected, script->get_graph_base());
        title = "Connect Selected Nodes";
    } else if (tool == "reconnect" || tool == "bypass" || tool == "bypass_copy") {
        const bool keep = tool == "bypass";
        next = bypass_graph_selection(document, active_graph, selected, !keep, script->get_graph_base());
        title = keep ? "Bypass and Keep Nodes" : tool == "bypass_copy" ? "Bypass and Copy Nodes" : "Delete and Reconnect";
        if (next.is_empty() && !selected.is_empty()) {
            show_problem("Select an unambiguous execution or data chain with compatible endpoints.");
        } else if (!next.is_empty() && tool == "bypass_copy") {
            String error;
            const String encoded = encode_graph_clipboard(document, active_graph, script->get_instance_id(), selected, error);
            if (encoded.is_empty()) {
                if (!error.is_empty()) show_problem(error);
                return;
            }
            graph_clipboard = encoded;
            auto *display = DisplayServer::get_singleton();
            if (display->has_feature(DisplayServer::FEATURE_CLIPBOARD)) display->clipboard_set(encoded);
        }
    } else if (tool == "dissolve" && selected.size() == 1) {
        next = dissolve_reroute(document, active_graph, selected[0], script->get_graph_base());
        title = "Dissolve Reroute";
    } else if (tool == "break_pin") {
        next = break_pin_links(graph, command.get("node", 0), command.get("pin", StringName()), command.get("output", false));
        title = "Break Pin Links";
    } else if (tool == "reset_pin") {
        next = reset_pin_literal(graph, command.get("node", 0), command.get("pin", StringName()));
        title = "Reset Input to Default";
    }
    if (!next.is_empty()) {
        const Dictionary pruned = prune_graph_comment_members(next);
        commit_graph(title, pruned.is_empty() ? next : pruned);
        canvas->grab_focus();
    }
}

} // namespace crimson::visual_scripting

#endif
