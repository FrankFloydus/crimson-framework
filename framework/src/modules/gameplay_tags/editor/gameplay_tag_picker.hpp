// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <map>
#include <set>

namespace godot {
class Button;
class CheckBox;
class InputEvent;
class Label;
class LineEdit;
class MenuButton;
class Tree;
class TreeItem;
}

namespace crimson::editor_ui {
class EditorModal;
}

namespace crimson::gameplay_tags {

/// Shared editor dialog for selecting tags and authoring the project catalog.
class GameplayTagEditorPicker final : public godot::Window {
    GDCLASS(GameplayTagEditorPicker, godot::Window)

    /// Commands shown in the picker's Options menu.
    enum Command { NEW_ROOT, NEW_CHILD, RENAME, DELETE_TAG, COPY_NAME, EXPAND_ALL, COLLAPSE_ALL, CLEAR_SELECTION };

    /// Inspector row for the current invocation.
    godot::ObjectID source;
    /// Button to focus again when the dialog closes.
    godot::ObjectID return_focus;
    /// Stable Inspector action used if an edit removes the opening chip.
    godot::ObjectID fallback_focus;
    /// Commits a detached choice and rejects a stale edit.
    godot::Callable submit;
    /// Allows multiple explicit entries when editing a container.
    bool multiple = false;
    /// Maintains the authored insertion order while checking and unchecking entries.
    godot::PackedStringArray choices;
    /// Last observed catalog paths, including implicit ancestors.
    std::set<godot::String> catalog_paths;
    /// Maps paths to non-owning tree rows during the current tree generation.
    std::map<godot::String, godot::TreeItem *> items;
    /// Retains collapse state across filtering and new declarations.
    std::map<godot::String, bool> branch_collapsed;
    /// Search input that receives focus when the dialog opens.
    godot::LineEdit *search = nullptr;
    /// Command menu containing catalog actions and view controls.
    godot::MenuButton *options = nullptr;
    /// Compact native hierarchy styled with the shared editor theme.
    godot::Tree *tree = nullptr;
    /// Current status or correction.
    godot::Label *status = nullptr;
    /// Applies the pending selection to the Inspector field.
    godot::Button *apply = nullptr;
    /// Shared modal composition for catalog changes.
    editor_ui::EditorModal *catalog_prompt = nullptr;
    /// Path entry shown for create and rename.
    godot::LineEdit *path_entry = nullptr;
    /// Option to rename a tag and its descendants together.
    godot::CheckBox *rename_children = nullptr;
    /// The active catalog action while its prompt is open.
    Command catalog_action = NEW_ROOT;
    /// Existing catalog path affected by the current command.
    godot::String catalog_target;

    /// Return the currently focused path or an empty string.
    godot::String selected_path() const;
    /// Load the current project catalog and resolve pending selections through redirects.
    void refresh_catalog();
    /// Check whether the current catalog contains this path, including implicit parents.
    bool is_catalog_path(const godot::String &path) const;
    /// Rebuild the visible hierarchy with an optional path to reveal.
    void rebuild_tree(const godot::String &reveal = {});
    /// Reconcile checkboxes with exact selections and their catalog ancestors.
    void update_choices();
    /// Show the given count, error, or empty state without changing the current selection.
    void update_status();
    /// Update only the hierarchy when a search term changes.
    void filter_changed(const godot::String &filter);
    /// Move focus from search to the selected or first result.
    void focus_results(const godot::String &text);
    /// Track branch disclosure for when a search is cleared.
    void branch_toggled(godot::Object *item);
    /// Apply a clicked checkbox to the current draft.
    void item_edited();
    /// Toggle the focused tag when using Enter or a double-click.
    void item_activated();
    /// Apply a checked-state change to the draft and refresh the visible tree.
    void set_chosen(const godot::String &path, bool chosen);
    /// Choose the explicit tag in place of its explicit ancestors.
    void select_tag(const godot::String &path);
    /// Clear an explicit tag or branch and keep its parent when appropriate.
    void unselect_tag(const godot::String &path);
    /// Submit the draft and close when the owner still accepts the edit.
    void apply_choices();
    /// Discard the draft and return to the calling field.
    void cancel();
    /// Release references and dismiss this invocation.
    void finish(bool restore_focus);
    /// Update command availability from the selected branch.
    void update_options();
    /// Run a view command or open the required management prompt.
    void run_command(int64_t command);
    /// Configure the catalog prompt for add, rename, or delete.
    void prompt_catalog(Command action);
    /// Commit the current catalog edit, retaining the prompt on failure.
    bool perform_catalog_action();

protected:
    /// The native inspector dialog exposes no runtime methods.
    static void _bind_methods() {}

public:
    /// Close via the normal cancel path for keyboard or controller Back.
    void _input(const godot::Ref<godot::InputEvent> &event) override;
    /// Build the window once under the editor's lifetime owner.
    void configure();
    /// Open against one Inspector edit and its detached starting values.
    void open(const godot::PackedStringArray &initial, bool is_multiple, const godot::String &reveal,
            godot::ObjectID source_id, godot::ObjectID focus_id, godot::ObjectID fallback_id,
            const godot::Callable &on_submit);
    /// Close without restoring focus when the active Inspector row is removed or disabled.
    void dismiss_source(godot::ObjectID source_id);
};

/// Return the editor-owned picker while the native editor plugin is active.
GameplayTagEditorPicker *find_gameplay_tag_picker();

} // namespace crimson::gameplay_tags

#endif
