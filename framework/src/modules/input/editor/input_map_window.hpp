// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "modules/input/controls.hpp"

#include <godot_cpp/classes/window.hpp>
#include <set>
#include <vector>

namespace godot {
class Button;
class EditorFileDialog;
class Label;
class LineEdit;
class PopupMenu;
class ScrollContainer;
class Tree;
class VBoxContainer;
}

namespace crimson::editor_ui {
class Picker;
class EditorTree;
class EditorModal;
}

namespace crimson::input {

// Author one InputActionMap asset in a detached draft.
class InputMapWindow : public godot::Window {
    GDCLASS(InputMapWindow, godot::Window)

    struct SourceChoice {
        godot::StringName id; ///< Stable choice for the picker, independent of its visible name.
        godot::String label; ///< Friendly physical control name.
        godot::String group; ///< Expandable device family.
        godot::Ref<godot::InputEvent> event; ///< Normalized physical event when the choice uses one control.
        InputBinding::Stick stick = InputBinding::NONE; ///< Whole-stick source when the choice supplies two axes.
        InputBinding::MouseAxes mouse_axes = InputBinding::MOUSE_XY; ///< Physical mouse axes represented by this choice.
    };

    enum class FilePurpose { SAVE_MAP, SAVE_COPY, SAVE_ACTION };
    enum class Setting { DEAD_ZONE_MODE, DEAD_ZONE_LOWER, DEAD_ZONE_UPPER, OUTPUT_X, OUTPUT_Y, INVERT_X, INVERT_Y, SCALE_X, SCALE_Y, RESPONSE_EXPONENT };
    enum class Removal { NONE, ACTION, BINDING, MODIFIER };

    godot::Ref<InputActionMap> source; ///< Asset currently being edited, or null until first save.
    godot::Ref<InputActionMap> draft; ///< Detached map edited by the user.
    godot::Ref<InputActionMap> saved; ///< Snapshot last saved by this window.
    godot::PackedByteArray source_revision; ///< Source state last loaded or saved.
    std::vector<godot::Ref<InputActionMap>> history; ///< Copy-on-write draft history.
    size_t revision = 0; ///< Position in the draft history.
    godot::Ref<InputActionMap> next_source; ///< Next map requested while the draft is dirty.
    godot::Ref<InputLayout> next_layout; ///< Optional layout receiving the newly created map after discard.
    godot::ObjectID next_owner; ///< Optional Controls/Profile receiving the new map after discard.
    godot::Ref<InputLayout> assignment_layout; ///< Empty layout or container when a player starts without a map.
    godot::ObjectID assignment_owner; ///< Scene object eligible to receive a newly created layout.
    godot::Ref<InputAction> new_action; ///< Pending reusable definition awaiting a standalone file name.
    godot::StringName selected_slot; ///< Current modifier inspector target.
    godot::StringName displayed_binding; ///< Last inspector target, used to preserve scrolling during edits.
    godot::StringName focused_row; ///< Row to restore when an edit rebuilds the tree.
    godot::StringName picker_focus_slot; ///< Binding whose picker should regain focus after a manual choice.
    godot::StringName listening_slot; ///< The only binding currently armed for raw capture.
    godot::ObjectID listen_button; ///< Button whose listening tint is removed when capture ends.
    godot::StringName listen_icon; ///< Neutral device icon restored when capture ends.
    std::set<godot::StringName> collapsed; ///< Branches collapsed before a search temporarily opens them.
    std::set<godot::StringName> rendered_actions; ///< Parent rows in the last tree rebuild.
    std::vector<SourceChoice> choices; ///< Complete physical-control catalog.
    bool displayed_filter = false; ///< Previous tree was built from search results.
    bool loading_map = false; ///< Skip search notifications while restoring a map draft.
    bool redraw_pending = false; ///< Coalesces rebuilds after widget signals return.
    bool modifier_refresh_pending = false; ///< Coalesces inspector updates without rebuilding rows.
    FilePurpose file_purpose = FilePurpose::SAVE_MAP; ///< Context for the file dialog's response.

    godot::Label *counts = nullptr; ///< Full action and binding counts.
    godot::Label *status = nullptr; ///< Save, validation, and capture feedback.
    godot::LineEdit *search = nullptr; ///< Filter by action, stable ID, or physical control.
    godot::Button *save_button = nullptr; ///< Applies the current draft.
    godot::PopupMenu *map_commands = nullptr; ///< Footer commands and current undo/redo availability.
    godot::ScrollContainer *binding_scroll = nullptr; ///< Scroll position of the action tree.
    godot::ScrollContainer *modifier_scroll = nullptr; ///< Scrollable settings below the fixed binding context.
    godot::Control *binding_drag_rail = nullptr; ///< Indicates insertion among the current map rows.
    godot::Control *modifier_drag_rail = nullptr; ///< Indicates insertion among the current binding's modifiers.
    crimson::editor_ui::EditorTree *bindings = nullptr; ///< Shared tree for action and binding rows.
    godot::VBoxContainer *modifiers = nullptr; ///< Modifier stack and its settings.
    godot::EditorFileDialog *file_dialog = nullptr; ///< Project resource save picker.
    crimson::editor_ui::EditorModal *action_picker = nullptr; ///< Chooses an InputAction from indexed project resources.
    godot::LineEdit *action_query = nullptr; ///< Filters listed action assets by name and path.
    godot::Tree *action_results = nullptr; ///< Shared-theme results with the title and containing folder.
    godot::Control *action_empty = nullptr; ///< Current scanning or empty result message.
    godot::PackedStringArray action_paths; ///< Snapshot of typed project resources while the chooser is open.
    godot::Ref<InputAction> chosen_action; ///< Validated resource transferred from the chooser to the draft.
    bool scanning_actions = false; ///< Whether the editor is preparing the project filesystem index.
    crimson::editor_ui::EditorModal *discard_dialog = nullptr; ///< Protects dirty drafts before map switching.
    crimson::editor_ui::EditorModal *remove_dialog = nullptr; ///< Confirms one draft removal before changing history.
    crimson::editor_ui::EditorModal *action_dialog = nullptr; ///< Prompts for a new action definition.
    godot::LineEdit *action_name = nullptr; ///< New reusable action title.
    crimson::editor_ui::Picker *action_kind = nullptr; ///< New reusable action's value type.
    crimson::editor_ui::EditorModal *metadata_dialog = nullptr; ///< Edits map metadata or one mapping ID.
    godot::LineEdit *metadata_id = nullptr; ///< Map ID or script action ID.
    godot::LineEdit *metadata_title = nullptr; ///< Optional user-facing map title.
    godot::StringName metadata_mapping; ///< Target mapping ID, or empty when editing the map.
    Removal pending_removal = Removal::NONE; ///< Operation waiting for confirmation.
    godot::StringName removal_id; ///< Stable action or binding ID targeted by the dialog.
    godot::StringName order_focus_row; ///< Grip that should regain keyboard focus after a row move.
    int64_t removal_index = -1; ///< Modifier position at the time the dialog opened.
    godot::Ref<InputModifier> removal_modifier; ///< Protects an unrelated modifier from a stale index.

    // Read the current binding and optionally its containing mapping.
    godot::Ref<InputBinding> find_binding(const godot::StringName &id, godot::Ref<InputActionMapping> *mapping = nullptr) const;
    // Read a map declaration using its stable action ID.
    godot::Ref<InputActionMapping> find_mapping(const godot::StringName &id) const;
    // Replace one stable slot in a new map revision while sharing unaffected entries.
    godot::Ref<InputActionMap> replace_binding(const godot::Ref<InputBinding> &binding, const godot::Ref<InputBinding> &replacement) const;
    // Clone map-owned data and retain reusable InputAction references.
    static godot::Ref<InputActionMap> snapshot(const godot::Ref<InputActionMap> &map);
    // Start a new history without changing project resources.
    void load_map(const godot::Ref<InputActionMap> &map, const godot::Ref<InputLayout> &layout, godot::ObjectID owner);
    // Commit one local draft change after any active UI callbacks return.
    void stage(const godot::Ref<InputActionMap> &next, bool rebuild_rows = true, bool rebuild_modifiers = true);
    // Preserve expansion and selection while rebuilding the action tree.
    void redraw();
    // Queue a complete refresh without removing widgets from inside their own callbacks.
    void queue_redraw();
    // Rebuild the inspector for the selected binding only.
    void draw_modifiers();
    // Queue an inspector-only refresh after a modifier control completes its signal.
    void queue_modifier_refresh();
    // Refresh title, save status, and history controls.
    void refresh_frame();
    // Reflect the active query in the hierarchy.
    void filter(const godot::String &query);
    // Select a tree row without mutating the map draft.
    void select_row(const godot::StringName &row);
    // Add a row's device controls and optional remove action to the shared tree.
    void build_binding(const godot::Ref<InputBinding> &binding, const godot::Ref<InputActionMapping> &mapping, const godot::StringName &parent);
    // Attach an order handle to an action or binding row.
    godot::Button *add_row_order_handle(godot::Node *owner, godot::StringName item, godot::StringName parent,
            const godot::String &caption);
    // Move the focused row with the keyboard without activating its other controls.
    void shortcut_row_order(const godot::Ref<godot::InputEvent> &event, godot::StringName item, godot::StringName parent,
            godot::Button *handle);
    // Begin a same-draft row drag with a preview and stable resource IDs.
    godot::Variant begin_row_drag(const godot::Vector2 &point, godot::Button *handle, godot::StringName item,
            godot::StringName parent, const godot::String &caption);
    // Read the original row ID only when it belongs to the active draft and target parent.
    godot::StringName row_drag_source(const godot::Variant &data, godot::StringName parent) const;
    // Ignore invalid, cross-action, or stale drags.
    bool can_drop_row(const godot::Vector2 &point, const godot::Variant &data, godot::StringName item,
            godot::StringName parent, godot::Control *target, godot::Control *background) const;
    // Convert a drop on the upper or lower half into one undoable move.
    void drop_row(const godot::Vector2 &point, const godot::Variant &data, godot::StringName item,
            godot::StringName parent, godot::Control *target);
    // Reorder a map action or one binding within its own action.
    void reorder_row(godot::StringName source, godot::StringName parent, godot::StringName target, int placement);
    // Populate the catalog for keyboard, mouse, and gamepad picking.
    void build_choices();
    // Resolve a saved event to the visible catalog choice where one exists.
    godot::StringName choice_id(const godot::Ref<InputBinding> &binding) const;
    // Present an authored source without the engine's internal event format.
    godot::String binding_label(const godot::Ref<InputBinding> &binding) const;
    // Choose a physical source from the hierarchical picker.
    void choose_binding(const godot::StringName &choice, const godot::StringName &slot);
    // Use one edit path for both manual selection and captured input.
    void set_binding_source(const godot::StringName &slot, const godot::Ref<godot::InputEvent> &event,
            InputBinding::Stick stick, InputBinding::MouseAxes mouse_axes);
    // Add an empty stable binding slot to one action.
    void add_binding(godot::StringName mapping_id);
    // Remove the specified binding while preserving all other slot IDs.
    void remove_binding(godot::StringName slot);
    // Ask for confirmation before removing an action or binding.
    void prompt_removal(Removal operation, godot::StringName id, const godot::String &name);
    // Resolve the binding by ID and open its confirmation dialog.
    void prompt_binding_removal(godot::StringName slot);
    // Resolve the selected modifier and open its confirmation dialog.
    void prompt_modifier_removal(int64_t index);
    // Apply the pending removal only if the targeted item still exists.
    void confirm_removal();
    // Close the confirmation without changing the draft.
    void cancel_removal();
    // Arm one row for physical input capture.
    void start_listening(godot::StringName slot, godot::Button *device_button);
    // Restore the device button and release capture.
    void cancel_listening();
    // Add a new modifier to the end of the selected binding's stack.
    void add_modifier(const godot::StringName &type);
    // Change one setting on an ordered modifier.
    void change_setting(double value, int64_t index, int64_t setting);
    // Move an older binding's authored contribution into equivalent leading modifiers.
    void convert_contribution();
    // Move a modifier to the adjacent position in the stack.
    void move_modifier(int64_t index, int64_t direction);
    // Read an active modifier drag from the currently selected binding and draft.
    int64_t modifier_drag_source(const godot::Variant &data) const;
    // Start a native drag only from the ordered modifier heading.
    godot::Variant begin_modifier_drag(const godot::Vector2 &point, godot::Control *handle, godot::StringName slot,
            int64_t index, const godot::Ref<InputBinding> &binding);
    // Validate that a drag belongs to the same still-current modifier stack.
    bool can_drop_modifier(const godot::Vector2 &point, const godot::Variant &data, godot::StringName slot,
            int64_t index, godot::Control *target) const;
    // Insert the dragged modifier before or after the hovered card.
    void drop_modifier(const godot::Vector2 &point, const godot::Variant &data, godot::StringName slot,
            int64_t index, godot::Control *target);
    // Remove one modifier from the selected binding.
    void remove_modifier(godot::StringName slot, int64_t index);
    // Dispatch a command from the header menu.
    void map_command(int64_t command);
    // Dispatch a command from the action menu.
    void action_command(int64_t command, godot::StringName mapping_id);
    // Present settings for the map or one mapping's stable ID.
    void edit_metadata(godot::StringName mapping_id = {});
    // Apply edited metadata without changing existing binding IDs.
    void apply_metadata();
    // Keep the metadata form open until its ID is present and available.
    bool validate_metadata();
    // Open the editor-authored picker for reusable project action assets.
    void choose_action_asset();
    // Refresh the cached paths when opening the chooser or when the editor refreshes its resource index.
    void read_action_assets();
    // Finish a scan that did not add or remove indexed resources.
    void finish_action_scan(bool changed);
    // Show rows matching the current action search without loading their resources.
    void filter_action_assets(const godot::String &query);
    // Move from the search field into the available result rows with the arrow keys.
    void navigate_action_results(const godot::Ref<godot::InputEvent> &event);
    // Validate that the selected path still loads as an InputAction before closing the picker.
    bool validate_action_choice();
    // Add the validated definition to the map draft.
    void confirm_action_choice();
    // Present fields for a new standalone action.
    void prompt_action();
    // Offer a resource path for the newly configured action.
    void save_new_action();
    // Keep the new action form open until it has a display name.
    bool validate_action_name();
    // Add one reusable action definition with a new persistent map-local ID.
    void add_action(const godot::Ref<InputAction> &action);
    // Open the asset picker for independently saved maps.
    void choose_map_asset();
    // Open the selected map resource.
    void map_chosen(const godot::String &path);
    // Open the shipped map for use as a starter and retain its path.
    void open_preset();
    // Start a new empty draft without replacing an existing layout map.
    void new_map();
    // Step back through local draft changes.
    void undo();
    // Step forward through local draft changes.
    void redo();
    // Save or prompt for a path when the map has not been saved.
    void save();
    // Prompt for a separate copy of the current map.
    void save_copy();
    // Select a project resource destination for the supplied purpose.
    void pick_save_path(FilePurpose purpose, const godot::String &name);
    // Persist a newly selected map/action path after checking collisions.
    void file_selected(const godot::String &path);
    // Release the pending action when its native file chooser is canceled.
    void cancel_file_selection();
    // Persist a standalone map before updating the editor's cached resource.
    void save_map(const godot::String &path, bool new_file);
    // Add an authored map to a previously empty layout or Controls assignment.
    void assign_first_map(const godot::Ref<InputActionMap> &map);
    // Switch to the next map only after the user approves discarding edits.
    void confirm_discard();
    // Release a deferred source switch when the user keeps the current draft.
    void cancel_discard();
    // Hide the floating editor without losing the current draft.
    void close_window();
    // Return the prompt that currently owns workbench input, if one is open.
    crimson::editor_ui::EditorModal *active_modal() const;

protected:
    // This authoring window exposes no runtime bindings.
    static void _bind_methods();
    // Clear insertion feedback after a native drag or when the pointer leaves the window.
    void _notification(int what);

public:
    // Construct the floating authoring window and its persistent dialogs.
    void build();
    // Open one map, optionally setting a new-map assignment context.
    void open(const godot::Ref<InputActionMap> &map, const godot::Ref<InputLayout> &layout = {}, godot::Object *owner = nullptr, bool resume = false);
    // Capture raw control input before it reaches normal focused widgets.
    void _input(const godot::Ref<godot::InputEvent> &event) override;
};

} // namespace crimson::input

#endif
