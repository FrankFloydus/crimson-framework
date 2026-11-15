// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_custom_code.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_spacing.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/code_edit.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/gd_script_syntax_highlighter.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/popup_panel.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>
#include <utility>

namespace crimson::visual_scripting {
namespace {
using namespace godot;
namespace ui = crimson::editor_ui;
namespace spacing = ui::spacing;

/// Match the standard local LSP completion item kinds used by Godot's GDScript server.
CodeEdit::CodeCompletionKind completion_kind(int64_t kind) {
    switch (kind) {
        case 2: case 3: return CodeEdit::KIND_FUNCTION;
        case 5: case 10: return CodeEdit::KIND_MEMBER;
        case 6: return CodeEdit::KIND_VARIABLE;
        case 7: case 8: return CodeEdit::KIND_CLASS;
        case 13: return CodeEdit::KIND_ENUM;
        case 14: return CodeEdit::KIND_KEYWORD;
        case 15: return CodeEdit::KIND_NODE_PATH;
        case 17: return CodeEdit::KIND_FILE_PATH;
        case 21: return CodeEdit::KIND_CONSTANT;
        case 23: return CodeEdit::KIND_SIGNAL;
        default: return CodeEdit::KIND_PLAIN_TEXT;
    }
}

/// Focus the original control when it still exists, or the graph canvas after a rebuild.
void restore_editor_focus(uint64_t preferred, uint64_t fallback) {
    for (const uint64_t id : {preferred, fallback}) {
        if (auto *control = Object::cast_to<Control>(ObjectDB::get_instance(id));
                control && control->is_visible_in_tree() && control->get_focus_mode() != Control::FOCUS_NONE) {
            control->grab_focus();
            return;
        }
    }
}

/// Read the exact wrapper prefix for a visible line, including unindented string continuations.
int source_column_offset(const CustomScriptSource &source, int64_t visible_line) {
    return visible_line >= 0 && visible_line < source.body_columns.size()
            ? source.body_columns[visible_line] : source.body_indent;
}

/// Locate a visible source line in the generated GDScript after helper layout.
int source_line(const CustomScriptSource &source, int32_t visible_line) {
    return visible_line >= 0 && visible_line < source.body_lines.size()
            ? source.body_lines[visible_line] : source.body_line + visible_line;
}

/// Map a language-server location back to the visible body or return a context-only line.
int64_t visible_source_line(const CustomScriptSource &source, int64_t generated_line) {
    if (generated_line < 0) {
        return -1;
    }
    return source.body_lines.is_empty() ? generated_line - source.body_line : source.body_lines.find(generated_line);
}

} // namespace

using namespace godot;

void VisualScriptCustomCode::build() {
    set_name("VisualScriptCustomCodeDialog");
    set_force_native(EditorInterface::get_singleton()->is_multi_window_enabled());
    ui::embed_editor_popups(this);
    set_title("Custom Script");
    set_hide_on_ok(false);
    set_ok_button_text("Apply");
    set_cancel_button_text("Cancel");
    get_ok_button()->set_name("VisualScriptApplyCustomCode");
    get_ok_button()->set_theme_type_variation("CrimsonPrimary");
    get_cancel_button()->set_name("VisualScriptCancelCustomCode");
    set_process(false);

    auto *content = memnew(VBoxContainer);
    add_child(content);
    content->add_theme_constant_override("separation", ui::pixels(this, 10));
    auto *toolbar = memnew(HBoxContainer);
    content->add_child(toolbar);
    toolbar->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *title = ui::label(toolbar, "GDScript", ui::LabelStyle::HEADING);
    title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    diagnostics_button = ui::button(toolbar, {}, "Show GDScript diagnostics", callable_mp(this, &VisualScriptCustomCode::open_diagnostics),
            ui::ButtonStyle::QUIET);
    diagnostics_button->set_name("VisualScriptCustomCodeDiagnosticsTrigger");
    diagnostics_button->set_custom_minimum_size(Vector2(ui::pixels(this, 28), ui::pixels(this, 28)));
    if (const Ref<Theme> stock = EditorInterface::get_singleton()->get_editor_theme(); stock.is_valid()) {
        diagnostics_button->set_button_icon(stock->get_icon("NodeInfo", "EditorIcons"));
    }
    auto *hint = ui::label(content, "Declare pins with #@input or #@output before a typed variable.", ui::LabelStyle::MUTED);
    hint->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    hint->set_tooltip_text("Place #@input or #@output on its own line at the left margin, immediately above a typed var declaration. "
            "Use input names directly. Assign output names before Then.");
    hint->set_mouse_filter(Control::MOUSE_FILTER_PASS);
    editor = memnew(CodeEdit);
    content->add_child(editor);
    editor->set_name("VisualScriptCustomCodeEdit");
    editor->set_accessibility_name("Custom Script GDScript source");
    editor->set_custom_minimum_size(Vector2(ui::pixels(this, 420), ui::pixels(this, 230)));
    editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    editor->set_draw_line_numbers(true);
    editor->set_highlight_current_line(true);
    editor->set_auto_indent_enabled(true);
    editor->set_auto_brace_completion_enabled(true);
    editor->set_highlight_matching_braces_enabled(true);
    editor->set_code_completion_enabled(true);
    editor->add_theme_font_size_override("font_size", ui::pixels(this, 13));
    editor->add_theme_color_override("caret_color", ui::color(ui::Surface::TEXT));
    editor->add_theme_color_override("selection_color", ui::color(ui::Surface::SELECTED));
    TypedArray<String> prefixes;
    prefixes.append(".");
    editor->set_code_completion_prefixes(prefixes);
    editor->add_comment_delimiter("#", "", true);
    if (const Ref<Theme> stock = EditorInterface::get_singleton()->get_editor_theme();
            stock.is_valid() && stock->has_font("source", "EditorFonts")) {
        editor->add_theme_font_override("font", stock->get_font("source", "EditorFonts"));
    }
    Ref<GDScriptSyntaxHighlighter> highlighter;
    highlighter.instantiate();
    editor->set_syntax_highlighter(highlighter);

    diagnostics_popup = memnew(PopupPanel);
    diagnostics_popup->set_name("VisualScriptCustomCodeDiagnosticsPopup");
    diagnostics_popup->set_wrap_controls(true);
    diagnostics_popup->set_theme(get_theme());
    add_child(diagnostics_popup);
    diagnostics_popup->connect("popup_hide", callable_mp(this, &VisualScriptCustomCode::close_diagnostics));
    auto *diagnostics = memnew(VBoxContainer);
    diagnostics_popup->add_child(diagnostics);
    diagnostics->add_theme_constant_override("separation", ui::pixels(this, spacing::md));
    auto *diagnostics_header = memnew(HBoxContainer);
    diagnostics->add_child(diagnostics_header);
    ui::label(diagnostics_header, "GDScript diagnostics", ui::LabelStyle::HEADING)->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    ui::icon_button(diagnostics_header, "close", "Close diagnostics",
            callable_mp(static_cast<Window *>(diagnostics_popup), &Window::hide), ui::ButtonStyle::QUIET);
    results = memnew(ScrollContainer);
    diagnostics->add_child(results);
    results->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    results->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    results->set_focus_mode(Control::FOCUS_ALL);
    results->set_name("VisualScriptCustomCodeDiagnosticsResults");
    results->set_accessibility_name("GDScript diagnostics messages");
    status = ui::label(results, {}, ui::LabelStyle::BODY);
    status->set_name("VisualScriptCustomCodeDiagnostics");
    status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    editor->connect("text_changed", callable_mp(this, &VisualScriptCustomCode::update_source));
    editor->connect("text_set", callable_mp(this, &VisualScriptCustomCode::update_source));
    editor->connect("code_completion_requested", callable_mp(this, &VisualScriptCustomCode::request_completion));
    editor->connect("caret_changed", callable_mp(this, &VisualScriptCustomCode::caret_changed));
    connect("confirmed", callable_mp(this, &VisualScriptCustomCode::apply_draft));
    connect("visibility_changed", callable_mp(this, &VisualScriptCustomCode::visibility_changed));
    connect("focus_exited", callable_mp(this, &VisualScriptCustomCode::focus_exited));
    language.on_completion = [this](const Array &options) { receive_completion(options); };
    language.on_diagnostics = [this](const Array &results) { receive_diagnostics(results); };
    language.on_unavailable = [this](const String &message) { analyzer_unavailable(message); };
}

void VisualScriptCustomCode::edit(const String &body, Source source, Apply apply, Control *trigger, Control *fallback) {
    if (is_visible()) {
        editor->grab_focus();
        return;
    }
    on_source = std::move(source);
    on_apply = std::move(apply);
    previous_focus = trigger ? ObjectID(trigger->get_instance_id()) : ObjectID();
    fallback_focus = fallback ? ObjectID(fallback->get_instance_id()) : ObjectID();
    editor->set_text(body);
    editor->clear_undo_history();
    unavailable = String();
    popup_centered(Vector2i(ui::pixels(this, 720), ui::pixels(this, 500)));
    editor->grab_focus();
    set_process(true);
    publish_source(true);
}

void VisualScriptCustomCode::dismiss() {
    if (has_draft()) {
        suspended = false;
        previous_focus = ObjectID();
        fallback_focus = ObjectID();
        if (is_visible()) {
            hide();
        } else {
            visibility_changed();
        }
    }
}

void VisualScriptCustomCode::suspend() {
    if (is_visible()) {
        suspended = true;
        hide();
    }
}

void VisualScriptCustomCode::resume() {
    if (!suspended) return;
    suspended = false;
    popup_centered(Vector2i(ui::pixels(this, 720), ui::pixels(this, 500)));
    editor->grab_focus();
    set_process(true);
    publish_source(true);
}

bool VisualScriptCustomCode::has_draft() const {
    return is_visible() || suspended;
}

void VisualScriptCustomCode::_process(double) {
    language.poll();
}

void VisualScriptCustomCode::_exit_tree() {
    language.close();
    document_open = false;
    suspended = false;
    on_source = {};
    on_apply = {};
    previous_focus = ObjectID();
    fallback_focus = ObjectID();
}

void VisualScriptCustomCode::update_source() {
    if (is_visible() && on_source && editor->get_text() != analyzed_body) {
        publish_source(false);
    }
}

void VisualScriptCustomCode::publish_source(bool initial) {
    analyzed_body = editor->get_text();
    apply_error = String();
    completion_caret.reset();
    editor->cancel_code_completion();
    clear_diagnostics();
    current = on_source(analyzed_body);
    if (!current.error.is_empty()) {
        language.close();
        document_open = false;
        show_diagnostics(current.error, 1);
        return;
    }
    show_diagnostics(unavailable.is_empty() ? String("Checking GDScript...") : unavailable);
    if (initial || !document_open) {
        document_open = true;
        language.open(current.text);
    } else {
        language.update(current.text);
    }
}

void VisualScriptCustomCode::request_completion() {
    if (!is_visible() || !on_source || !editor->has_focus() || editor->get_caret_count() != 1) {
        return;
    }
    update_source();
    if (!document_open || !unavailable.is_empty()) {
        return;
    }
    completion_caret = Vector2i(editor->get_caret_column(), editor->get_caret_line());
    language.complete(source_line(current, completion_caret->y),
            source_column_offset(current, completion_caret->y) + completion_caret->x);
}

void VisualScriptCustomCode::caret_changed() {
    const Vector2i position(editor->get_caret_column(), editor->get_caret_line());
    if (completion_caret && *completion_caret != position) {
        completion_caret.reset();
    }
}

void VisualScriptCustomCode::receive_completion(const Array &options) {
    const Vector2i position(editor->get_caret_column(), editor->get_caret_line());
    if (!is_visible() || !editor->has_focus() || editor->get_caret_count() != 1 ||
            !completion_caret || *completion_caret != position || editor->get_text() != analyzed_body) {
        return;
    }
    completion_caret.reset();
    for (const Variant &value : options) {
        if (value.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary row = value;
        const String display = row.get("label", String());
        if (display.is_empty() || display.begins_with("__crimson_") || row.has("textEdit")) {
            continue;
        }
        const auto kind = completion_kind(row.get("kind", int64_t(0)));
        String insert = row.get("insertText", display);
        if (kind == CodeEdit::KIND_FUNCTION && display.contains("(") && !insert.contains("(")) {
            insert += "(";
        }
        editor->add_code_completion_option(kind, display, insert, ui::color(ui::Surface::TEXT));
    }
    editor->update_code_completion_options(true);
}

void VisualScriptCustomCode::receive_diagnostics(const Array &results) {
    if (!is_visible() || !document_open || !apply_error.is_empty() || editor->get_text() != analyzed_body) {
        return;
    }
    clear_diagnostics();
    PackedStringArray errors;
    PackedStringArray warnings;
    for (const Variant &value : results) {
        if (value.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary item = value;
        const String message = item.get("message", String());
        if (message.is_empty()) {
            continue;
        }
        const Dictionary range = item.get("range", Dictionary());
        const Dictionary start = range.get("start", Dictionary());
        const int64_t body_line = visible_source_line(current, start.get("line", -1));
        const int64_t column = std::max<int64_t>(0,
                static_cast<int64_t>(start.get("character", 0)) - source_column_offset(current, body_line));
        const bool error = static_cast<int64_t>(item.get("severity", 1)) == 1;
        if (body_line >= 0 && body_line < editor->get_line_count()) {
            const int32_t line = static_cast<int32_t>(body_line);
            const bool already_marked = marked_lines.has(line);
            if (!already_marked) {
                marked_lines.append(line);
            }
            if (error || !already_marked) {
                const Color marker = error ? Color(0.82f, 0.24f, 0.29f, 0.18f) : Color(0.85f, 0.63f, 0.22f, 0.14f);
                editor->set_line_background_color(line, marker);
            }
            auto &group = error ? errors : warnings;
            group.append(String(error ? "Error " : "Warning ") + String::num_int64(body_line + 1) + ":" +
                    String::num_int64(column + 1) + "  " + message);
        } else if (error) {
            errors.append(String("Script context: ") + message);
        }
    }
    String summary = String("\n").join(errors);
    if (!warnings.is_empty()) {
        if (!summary.is_empty()) {
            summary += "\n\n";
        }
        summary += String("\n").join(warnings);
    }
    show_diagnostics(summary.is_empty() ? String("No GDScript diagnostics.") : summary, errors.size(), warnings.size());
}

void VisualScriptCustomCode::analyzer_unavailable(const String &message) {
    unavailable = message.is_empty() ? String("The Godot GDScript analyzer is unavailable.") : message;
    if (apply_error.is_empty()) {
        show_diagnostics(unavailable);
    }
}

void VisualScriptCustomCode::show_diagnostics(const String &message, int64_t errors, int64_t warnings) {
    status->set_text(message);
    const String totals = String::num_int64(errors) + String(errors == 1 ? " error / " : " errors / ") +
            String::num_int64(warnings) + String(warnings == 1 ? " warning" : " warnings");
    diagnostics_button->set_text(errors + warnings == 0 ? String() : totals);
    diagnostics_button->set_accessibility_name("GDScript diagnostics");
    ui::tooltip(diagnostics_button, String("Show GDScript diagnostics. ") + totals + ".");
}

void VisualScriptCustomCode::open_diagnostics() {
    if (!is_visible() || diagnostics_popup->is_visible()) {
        return;
    }
    editor->cancel_code_completion();
    close_on_escape_before_diagnostics = get_close_on_escape();
    set_close_on_escape(false);
    const Vector2 edge = diagnostics_button->get_global_rect().get_end();
    const int width = std::clamp<int>(get_size().x - ui::pixels(this, spacing::xl), 1, ui::pixels(this, 384));
    const int y = static_cast<int>(edge.y) + ui::pixels(this, spacing::xs);
    const int height = std::clamp<int>(get_size().y - y - ui::pixels(this, spacing::md), 1, ui::pixels(this, 246));
    diagnostics_popup->popup_on_parent(Rect2i(std::max<int>(0, edge.x - width), y, width, height));
    results->set_v_scroll(0);
    results->grab_focus();
}

void VisualScriptCustomCode::close_diagnostics() {
    set_close_on_escape(close_on_escape_before_diagnostics);
    if (is_visible()) {
        diagnostics_button->grab_focus();
    }
}

void VisualScriptCustomCode::clear_diagnostics() {
    for (const int32_t line : marked_lines) {
        if (line < editor->get_line_count()) {
            editor->set_line_background_color(line, Color(0, 0, 0, 0));
        }
    }
    marked_lines.clear();
}

void VisualScriptCustomCode::apply_draft() {
    if (!on_apply) {
        return;
    }
    update_source();
    apply_error = on_apply(editor->get_text());
    if (apply_error.is_empty()) {
        // Rebuilding Details may invalidate the opening control after Apply.
        previous_focus = ObjectID();
        hide();
    } else {
        show_diagnostics(apply_error, 1);
        open_diagnostics();
    }
}

void VisualScriptCustomCode::visibility_changed() {
    if (is_visible()) {
        return;
    }
    set_process(false);
    diagnostics_popup->hide();
    language.close();
    document_open = false;
    focus_exited();
    clear_diagnostics();
    if (suspended) return;
    on_source = {};
    on_apply = {};
    const ObjectID preferred = std::exchange(previous_focus, ObjectID());
    const ObjectID fallback = std::exchange(fallback_focus, ObjectID());
    if (preferred.is_valid() || fallback.is_valid()) {
        callable_mp_static(&restore_editor_focus).bind(static_cast<uint64_t>(preferred), static_cast<uint64_t>(fallback)).call_deferred();
    }
}

void VisualScriptCustomCode::focus_exited() {
    completion_caret.reset();
    editor->cancel_code_completion();
}

} // namespace crimson::visual_scripting

#endif
