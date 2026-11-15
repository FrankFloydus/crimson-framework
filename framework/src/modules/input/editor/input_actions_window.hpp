// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "modules/input/controls.hpp"

#include <godot_cpp/classes/window.hpp>
#include <vector>

namespace godot {
class Button;
class EditorFileDialog;
class ItemList;
class Label;
class LineEdit;
class VBoxContainer;
}

namespace crimson::editor_ui {
class EditorModal;
class Picker;
}

namespace crimson::input {

// Edits one input asset in a detached draft and commits through the Godot editor.
class InputActionsWindow : public godot::Window {
    GDCLASS(InputActionsWindow, godot::Window)

    godot::Ref<InputLayout> source; ///< Existing asset or scene resource to update on Save.
    godot::Ref<InputLayout> draft; ///< Current immutable editing snapshot.
    godot::Ref<InputLayout> last_saved; ///< Snapshot last written or read from a resource.
    godot::PackedByteArray source_revision; ///< Serialized source content when this draft was opened or saved.
    std::vector<godot::Ref<InputLayout>> history; ///< Local undo and redo history before Save.
    size_t revision = 0; ///< Current local history index.
    godot::ObjectID assignment_owner; ///< Optional scene node or profile whose Input Layout picker opened the editor.
    godot::Ref<InputLayout> original_assignment; ///< Avoids overwriting a picker changed outside this window.
    godot::Ref<InputLayout> next_source; ///< Requested asset retained across discard confirmation.
    godot::ObjectID next_owner; ///< Assignment context for the requested asset.
    godot::StringName selected_map = "Gameplay"; ///< Visible action map.
    godot::StringName selected_action; ///< Stable selected action ID.
    godot::StringName pending_map; ///< Named empty map until its first action is added.
    godot::StringName listening_slot; ///< Stable ID of the selected binding during capture.
    godot::ObjectID listen_button; ///< Focus target after cancelling capture.
    bool redraw_pending = false; ///< Multiple changes in one event queue only one rebuild.
    bool opening_file = false; ///< True when the resource dialog is picking an existing layout.
    bool replacing_draft = false; ///< Ignore focus callbacks while discarding old detail controls.

    godot::Label *path_label = nullptr; ///< Current asset name and unsaved marker.
    godot::Label *status = nullptr; ///< Validation, save, and input capture feedback.
    godot::ItemList *maps = nullptr; ///< Map list with action counts.
    godot::ItemList *actions = nullptr; ///< Filtered actions in authoring order.
    godot::LineEdit *search = nullptr; ///< Search by action name or ID within the selected map.
    godot::VBoxContainer *details = nullptr; ///< Layout settings and selected action form.
    godot::Button *save_button = nullptr; ///< Save the current draft.
    godot::Button *undo_button = nullptr; ///< Undo a draft edit.
    godot::Button *redo_button = nullptr; ///< Redo a draft edit.
    godot::EditorFileDialog *file_dialog = nullptr; ///< Resource-only Open or Save As picker.
    crimson::editor_ui::EditorModal *discard_dialog = nullptr; ///< Confirms replacing a dirty draft.
    crimson::editor_ui::EditorModal *remove_dialog = nullptr; ///< Confirms a legacy action or binding removal.
    godot::StringName removal_action; ///< Stable action ID waiting for confirmation.
    godot::StringName removal_slot; ///< Optional binding ID waiting for confirmation.
    crimson::editor_ui::EditorModal *map_dialog = nullptr; ///< Prompts for a new map's name.
    godot::LineEdit *map_name = nullptr; ///< Proposed map name.
    crimson::editor_ui::EditorModal *action_dialog = nullptr; ///< Prompts for a new action's name and value type.
    godot::LineEdit *action_name = nullptr; ///< Proposed display name and base ID.
    crimson::editor_ui::Picker *action_kind = nullptr; ///< Value type of the proposed action.
    crimson::editor_ui::EditorModal *control_dialog = nullptr; ///< Manual physical control picker.
    crimson::editor_ui::Picker *control_device = nullptr; ///< Device family selected for a manual binding.
    godot::LineEdit *control_key = nullptr; ///< Godot key name for a keyboard binding.
    crimson::editor_ui::Picker *control_choice = nullptr; ///< Available mouse or controller controls.
    godot::PackedInt32Array control_choice_ids; ///< Device control IDs in the visible picker's order.
    godot::StringName manual_slot; ///< Binding ID being edited by the manual picker.

    // Start editing an asset after any pending draft has been resolved.
    void load_draft(const godot::Ref<InputLayout> &layout, godot::ObjectID owner);
    // Clone the current draft, replacing one immutable action.
    godot::Ref<InputLayout> replace_action(const godot::Ref<InputAction> &entry) const;
    // Read the selected action by ID.
    godot::Ref<InputAction> current_action() const;
    // Publish one draft revision and schedule a refresh after the active UI signal.
    void stage(const godot::Ref<InputLayout> &next);
    // Retain a consistent selection when rebuilding the lists and form.
    void redraw();
    // Coalesce refreshes and avoid deleting a control from within its event handler.
    void queue_redraw();
    // Rebuild the selected action's editable metadata and bindings.
    void build_details(const godot::Ref<InputAction> &entry);
    // Add a compact row for one binding and its directional contribution.
    void build_binding(godot::VBoxContainer *panel, const godot::Ref<InputBinding> &binding, InputAction::ValueKind kind);
    // Edit one bound property of the selected action.
    void edit_action(const godot::String &value, godot::StringName action, godot::StringName property);
    // Apply a line edit when focus leaves without pressing Enter.
    void finish_action_field(godot::LineEdit *field, godot::StringName action, godot::StringName property);
    // Edit the selected action's analog neutral zone.
    void edit_deadzone(const godot::String &value, godot::StringName action);
    // Apply dead zone on focus loss as well as explicit Enter.
    void finish_deadzone(godot::LineEdit *field, godot::StringName action);
    // Set the selected action's pointer capture requirement.
    void edit_pointer_requirement(bool value);
    // Update the selected action's map from the picker.
    void move_action(int64_t index, godot::PackedStringArray names);
    // Edit the default pointer behavior for this layout.
    void edit_click_capture(bool value);
    // Pick the action that releases pointer capture, if any.
    void edit_release_action(int64_t index, godot::PackedStringArray action_ids);
    // Set the current map and select its first visible action.
    void select_map(int64_t index);
    // Set the selected action, without touching its saved configuration.
    void select_action(int64_t index);
    // Refresh filtered action rows as the query changes.
    void filter_actions(const godot::String &query);
    // Show a name prompt for a new action map.
    void prompt_map();
    // Create the named empty map and immediately offer its first action.
    void add_map();
    // Require a nonempty, unused name before advancing to the first action.
    bool validate_map_name();
    // Show the name and type prompt for a new action.
    void prompt_action();
    // Append a logical action with a unique, stable starting ID.
    void add_action();
    // Require a display name without dismissing the action form.
    bool validate_action_name();
    // Remove the selected action from the draft.
    void remove_action();
    // Ask before removing the selected legacy action and its bindings.
    void prompt_remove_action();
    // Ask before removing one legacy binding slot.
    void prompt_remove_binding(godot::StringName slot);
    // Apply the request only if the original action is still selected.
    void confirm_remove();
    // Clear a canceled removal without adding a draft revision.
    void cancel_remove();
    // Add any missing move, jump, mouse aim, and stick aim defaults.
    void add_player_defaults();
    // Add Escape and controller Start as the standard settings action.
    void add_settings_shortcut();
    // Add a binding or apply a complete directional preset.
    void add_binding(int64_t preset);
    // Apply the selected preset after mapping the compact picker index to its command.
    void add_binding_preset(int64_t index);
    // Change a binding's scalar or vector contribution using a directional choice.
    void set_direction(int64_t index, godot::StringName slot, godot::PackedVector3Array contributions);
    // Replace a binding's physical event while preserving its stable slot.
    void set_binding_event(const godot::StringName &slot, const godot::Ref<godot::InputEvent> &event);
    // Open a keyboard, mouse or gamepad picker without requiring a connected device.
    void choose_binding(godot::StringName slot);
    // Show controls for the selected device family.
    void select_control_device(int64_t index);
    // Validate the manually chosen control before replacing the binding.
    void apply_control_choice();
    // Keep the control form open when the keyboard key name cannot be resolved.
    bool validate_control_choice();
    // Remove a binding, leaving the remaining stable slots untouched.
    void remove_binding(godot::StringName slot);
    // Capture the next eligible input for a particular binding slot.
    void start_listening(godot::StringName slot, godot::Button *button);
    // Release capture and restore focus after cancelling.
    void cancel_listening();
    // Step through the local draft history.
    void undo();
    void redo();
    // Start a new draft, retaining a Controls/Profile assignment context.
    void new_layout();
    // Open the resource picker and attach the selected asset after validation.
    void open_layout();
    // Persist the current draft at the source path or offer Save As.
    void save();
    // Apply the selected path from the resource picker.
    void file_selected(const godot::String &path);
    // Persist an external resource and apply its changes to any cached source.
    void save_external(const godot::String &path);
    // Apply a draft or a newly selected resource to the Godot editor with undo.
    void commit_source();
    // Apply a selected resource to the originating Controls or Profile.
    void assign_source();
    // Open the next requested draft after the developer confirms discarding edits.
    void confirm_discard();
    // Release a deferred layout switch when the user keeps the current draft.
    void cancel_discard();
    // Hide without dropping edits and end raw input capture.
    void close_window();
    // Return the prompt that currently owns workbench input, if one is open.
    crimson::editor_ui::EditorModal *active_modal() const;
    // Reflect current source and history status in the toolbar.
    void refresh_toolbar();
    // Show actionable validation feedback for the current draft.
    void show_validation();

protected:
    // This editor window has no gameplay bindings.
    static void _bind_methods();

public:
    // Build the window once under the native editor plugin.
    void build();
    // Edit the supplied layout or show the most recent draft.
    void open(const godot::Ref<InputLayout> &layout, godot::Object *owner, bool resume = false);
    // Handle window-local listening and save or undo keyboard shortcuts.
    void _input(const godot::Ref<godot::InputEvent> &event) override;
};

} // namespace crimson::input

#endif
