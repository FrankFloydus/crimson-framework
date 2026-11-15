// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/editor/visual_script_lsp.hpp"
#include "modules/visual_scripting/visual_script_custom_source.hpp"

#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <functional>
#include <optional>

namespace godot {
class Button;
class CodeEdit;
class Control;
class Label;
class PopupPanel;
class ScrollContainer;
}

namespace crimson::visual_scripting {

/// Edits a single isolated Custom Script node draft using the stock GDScript editor controls.
class VisualScriptCustomCode final : public godot::ConfirmationDialog {
    GDCLASS(VisualScriptCustomCode, godot::ConfirmationDialog)

public:
    /// Construct the wrapper without running the authored code.
    using Source = std::function<CustomScriptSource(const godot::String &)>;
    /// Commit the exact node revision, or return feedback while retaining the draft.
    using Apply = std::function<godot::String(const godot::String &)>;

    /// Build the window and install its editor and language service.
    void build();
    /// Open a fresh draft rooted at a node's current body and restore a valid focus target on close.
    void edit(const godot::String &body, Source source, Apply apply, godot::Control *trigger, godot::Control *fallback);
    /// Cancel the draft and release the language service before replacing the workspace context.
    void dismiss();
    /// Stop the analyzer and hide a draft while its workspace is not selected.
    void suspend();
    /// Reopen the same in-memory draft when the workbench becomes active again.
    void resume();
    /// Report whether this editor owns a draft that needs an explicit Apply or Cancel.
    bool has_draft() const;
    /// Progress the local analyzer only while this modal is open.
    void _process(double delta) override;
    /// Release the unsaved analyzer document when the workspace is removed.
    void _exit_tree() override;

protected:
    /// The graph owns this private modal.
    static void _bind_methods() {}

private:
    /// The only writable node source, kept separate until Apply.
    godot::CodeEdit *editor = nullptr;
    /// Opens the diagnostic list and displays the current totals.
    godot::Button *diagnostics_button = nullptr;
    /// Owns focus while the diagnostics are being inspected.
    godot::PopupPanel *diagnostics_popup = nullptr;
    /// A body-relative summary of current analyzer results inside the popup.
    godot::Label *status = nullptr;
    /// Scrollable message region focused while the popup is active.
    godot::ScrollContainer *results = nullptr;
    /// Parent Escape policy to restore after the popup is dismissed.
    bool close_on_escape_before_diagnostics = true;
    /// Dedicated in-memory virtual document bound to this draft.
    VisualScriptLsp language;
    /// Exact wrapper and visible source mapping for the current editor text.
    CustomScriptSource current;
    /// Builds the wrapper with the node's captured pins and graph declarations.
    Source on_source;
    /// Commits the source to the exact matching graph node.
    Apply on_apply;
    /// Deferred focus restoration targets, without retaining freed Details controls.
    godot::ObjectID previous_focus;
    godot::ObjectID fallback_focus;
    /// Visible caret that requested completion from this draft.
    std::optional<godot::Vector2i> completion_caret;
    /// Source text last sent to the language service.
    godot::String analyzed_body;
    /// Failure of the analyzer connection for this draft, if any.
    godot::String unavailable;
    /// Rejected Apply feedback that takes priority over late analyzer results.
    godot::String apply_error;
    /// Whether the editor has opened a wrapper document.
    bool document_open = false;
    /// Keep the text and callbacks while the main-screen workspace is hidden.
    bool suspended = false;
    /// Body lines whose backgrounds mark diagnostics.
    godot::PackedInt32Array marked_lines;

    /// Edit and analyze a new body without altering the graph.
    void update_source();
    /// Build and publish the latest text if a deferred editor signal has not done so.
    void publish_source(bool initial);
    /// Request stock completion at the visible caret mapped into the wrapper.
    void request_completion();
    /// Invalidate returned choices when the visible caret moves.
    void caret_changed();
    /// Show current stock GDScript completion candidates through CodeEdit.
    void receive_completion(const godot::Array &options);
    /// Display GDScript diagnostics with only the user's body line numbers.
    void receive_diagnostics(const godot::Array &results);
    /// Keep the editor usable when the analyzer cannot connect.
    void analyzer_unavailable(const godot::String &message);
    /// Update the popup content and expose diagnostic totals beside the icon.
    void show_diagnostics(const godot::String &message, int64_t errors = 0, int64_t warnings = 0);
    /// Display the results in a popup anchored below the toolbar button.
    void open_diagnostics();
    /// Restore the parent Escape policy and toolbar focus.
    void close_diagnostics();
    /// Clear old diagnostic markers when the buffer changes or closes.
    void clear_diagnostics();
    /// Apply one draft without saving the containing resource.
    void apply_draft();
    /// Stop analysis and restore keyboard focus once the modal hides.
    void visibility_changed();
    /// Clear completion on focus loss without discarding the draft.
    void focus_exited();
};

} // namespace crimson::visual_scripting

#endif
