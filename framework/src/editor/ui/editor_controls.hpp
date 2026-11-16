// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>

#include <vector>

namespace godot {
class Button;
class ButtonGroup;
class CheckBox;
class CheckButton;
class Control;
class FileDialog;
class HSlider;
class Label;
class LineEdit;
class MenuButton;
class Node;
class PopupPanel;
class PanelContainer;
class ProgressBar;
class ScrollContainer;
class StyleBoxFlat;
class TabContainer;
class TextEdit;
class Texture2D;
class TextureRect;
class Window;
}

namespace crimson::editor_ui {

/// Visual emphasis for a command button. Icon-only commands use the same button and a tooltip.
enum class ButtonStyle { SECONDARY, PRIMARY, QUIET, DESTRUCTIVE, ROW };

/// Text density and emphasis on a tool-owned label.
enum class LabelStyle { BODY, MUTED, HEADING, TITLE };

/// Editing availability. Read-only text can be focused and copied with the keyboard.
enum class FieldState { EDITABLE, READ_ONLY, DISABLED, INVALID };

/// State presented by a checkbox when edits apply to multiple selections.
enum class CheckState { UNCHECKED, CHECKED, MIXED };

/// Scoped editor colors by visual role. Canvas is the shell and Header is the main work surface.
enum class Surface { CANVAS, HEADER, RAISED, RULE, OUTLINE, TEXT, MUTED, HOVER, SELECTED, HIGHLIGHT, DANGER,
    SIDEBAR, ACCENT, DISABLED };

/// Logical corner sizes for editor controls and popup surfaces.
inline constexpr int control_corner_radius = 8;
inline constexpr int popup_corner_radius = 12;

/// Look up a shared editor color.
godot::Color color(Surface role);

/// A flat square surface with one logical pixel of border and optional content insets.
godot::Ref<godot::StyleBoxFlat> box(godot::Color background, godot::Color outline, int inset_x = 0, int inset_y = 0, double scale = 1.0);

/// Apply a rounded frame with the same border and inset behavior as a flat surface.
godot::Ref<godot::StyleBoxFlat> rounded_box(godot::Color background, godot::Color outline, int inset_x,
        int inset_y, double scale, int corner = control_corner_radius);

/// Create an editor-scoped Theme using Inter and the given editor scale.
godot::Ref<godot::Theme> create_theme(const godot::Window *owner, double scale = 1.0);

/// Render authored popups inside their tool window so rounded corners reveal the underlying controls.
void embed_editor_popups(godot::Window *owner);

/// Convert a logical length to pixels using a host Control or Window's editor theme.
int pixels(godot::Node *host, double logical);

/// Create a styled button attached to its owner. An optional existing shortcut is displayed as help.
godot::Button *button(godot::Node *owner, const godot::String &text, const godot::String &help = {},
        const godot::Callable &command = {}, ButtonStyle style = ButtonStyle::SECONDARY,
        godot::Key shortcut = godot::KEY_NONE);

/// Return a named compact line icon, optionally in the active device-capture ink.
godot::Ref<godot::Texture2D> icon(godot::Node *owner, const godot::StringName &name, bool highlighted = false);

/// Create an icon-only command with accessible help and an optional existing shortcut for display.
godot::Button *icon_button(godot::Node *owner, const godot::StringName &symbol, const godot::String &help,
        const godot::Callable &command = {}, ButtonStyle style = ButtonStyle::SECONDARY,
        godot::Key shortcut = godot::KEY_NONE);

/// Create a label in the requested text style.
godot::Label *label(godot::Node *owner, const godot::String &text, LabelStyle style = LabelStyle::BODY);

/// Create a quiet, keyboard-focusable handle for dragging an ordered row.
godot::Button *drag_grip(godot::Node *owner, const godot::String &help);

/// Show a compact badge beside the pointer until Godot ends the drag.
void show_drag_preview(godot::Control *source, const godot::String &caption,
        const godot::Ref<godot::Texture2D> &glyph = {}, const godot::Color &glyph_color = godot::Color(1, 1, 1),
        const godot::StringName &preview_name = {});

/// Add an input-transparent insertion rail to a viewport-owned container.
godot::Control *drag_insertion_rail(godot::Control *owner);

/// Mark the boundary of a target row. Both rectangles are in viewport coordinates.
void place_drag_insertion_rail(godot::Control *rail, godot::Rect2 row, godot::Rect2 clip, bool after);

/// Create a single-line input with optional placeholder and documented editing state.
godot::LineEdit *text_input(godot::Node *owner, const godot::String &text = {}, const godot::String &help = {},
        FieldState state = FieldState::EDITABLE, const godot::String &placeholder = {});

/// Create a search input with a built-in clear control.
godot::LineEdit *search_input(godot::Node *owner, const godot::String &placeholder, const godot::String &help = {});

/// Create a search field with the shared popup typography and row height.
godot::LineEdit *popup_search_input(godot::Node *owner, const godot::String &placeholder, const godot::String &help = {});

/// Create a multiline editor with the same input state as a single-line field.
godot::TextEdit *text_area(godot::Node *owner, const godot::String &text, const godot::String &help = {},
        FieldState state = FieldState::EDITABLE);

/// A compact numeric field with separate increment and decrement buttons.
class NumberInput final : public godot::HBoxContainer {
    GDCLASS(NumberInput, godot::HBoxContainer)

    /// Editor where the user can enter a value directly.
    godot::LineEdit *entry = nullptr;
    /// Button that subtracts one step from the current value.
    godot::Button *decrease = nullptr;
    /// Button that adds one step to the current value.
    godot::Button *increase = nullptr;
    /// Last accepted value.
    double value = 0.0;
    /// Inclusive lower bound.
    double lower = 0.0;
    /// Inclusive upper bound.
    double upper = 1.0;
    /// Adjustment quantum in the displayed value's units.
    double increment = 1.0;
    /// Whether the user may commit new values.
    FieldState availability = FieldState::EDITABLE;
    /// Optional read-only suffix shown beside the numeric editor.
    godot::Label *unit_label = nullptr;
    /// Receives the new double after a user edit is accepted.
    godot::Callable on_change;

    /// Commit a valid text edit, or restore the accepted value if parsing fails.
    void submit(const godot::String &text);
    /// Reconcile a pending edit when the field loses focus.
    void finish_editing();
    /// Apply a step in either direction.
    void step_by(double direction);
    /// Synchronize value presentation and increment availability.
    void refresh();
    /// Clamp to the supported interval and notify on a user value change.
    void apply(double next, bool notify);

protected:
    /// This internal component exposes no script bindings.
    static void _bind_methods() {}

public:
    /// Configure a numeric field with limits, step and commit callback.
    void configure(const godot::String &name, double initial, double minimum, double maximum,
            double step, const godot::Callable &changed, FieldState state, const godot::String &help);
    /// Change the displayed value without calling the commit callback.
    void set_value(double next);
    /// Show a unit without changing the value accepted by the input.
    void set_unit(const godot::String &unit);
    /// Present an invalid draft value while retaining the last valid value.
    void set_invalid_draft(const godot::String &text);
    /// Read the last committed value.
    double get_value() const;
};

/// Attach a numeric field. Changed receives the new value on a successful commit.
NumberInput *number_input(godot::Node *owner, const godot::String &name, double value, double min, double max,
        double step, const godot::Callable &changed = {}, FieldState state = FieldState::EDITABLE,
        const godot::String &help = {});

/// Attach an editable slider and an Inter value readout. Changed receives the current double.
godot::HSlider *slider(godot::Node *owner, const godot::String &name, double value, double min, double max,
        double step, const godot::Callable &changed = {}, FieldState state = FieldState::EDITABLE,
        const godot::String &unit = {});

/// Attach a checkbox, including a mixed initial value. Toggled receives the new bool.
godot::CheckBox *checkbox(godot::Node *owner, const godot::String &text, CheckState state = CheckState::UNCHECKED,
        const godot::Callable &toggled = {}, bool disabled = false);

/// Attach a switch. Toggled receives the new bool.
godot::CheckButton *toggle_switch(godot::Node *owner, const godot::String &text, bool on,
        const godot::Callable &toggled = {}, bool disabled = false);

/// Attach one radio option to a shared exclusive ButtonGroup.
godot::CheckBox *radio(godot::Node *owner, const godot::String &text, const godot::Ref<godot::ButtonGroup> &group,
        bool selected = false, const godot::Callable &toggled = {}, bool disabled = false);

/// One rich picker item. The picker owns detached optional content once supplied.
struct PickerOption {
    /// Stable application identifier passed back when the option is chosen.
    godot::StringName id;
    /// Accessible label and searchable text.
    godot::String label;
    /// Optional group that can be expanded in a searchable picker.
    godot::String group;
    /// Optional display-only content, such as an icon and two text lines.
    godot::Control *content = nullptr;
    /// Optional display-only content for the trigger when this option is selected.
    godot::Control *selected_content = nullptr;
    /// Whether the row is shown but cannot be chosen.
    bool disabled = false;
    /// Optional leading marker beside the text of a simple row.
    godot::Ref<godot::Texture2D> leading_icon;
};

/// Choose the presentation and value type for a picker.
enum class PickerMode { RICH, SEARCHABLE, COMPACT };

/// Visual treatment for finite option lists. Action forms may share the command menu density.
enum class SelectStyle { STANDARD, MENU };

/// Compact anchored selector that accepts caller-supplied option content.
class Picker final : public godot::VBoxContainer {
    GDCLASS(Picker, godot::VBoxContainer)

    /// Focusable anchor and selected value.
    godot::Button *trigger = nullptr;
    /// Optional type marker before the anchor label. The button keeps its trailing chevron.
    godot::TextureRect *trigger_marker = nullptr;
    /// Scoped popup owned by this picker.
    godot::PopupPanel *popup_panel = nullptr;
    /// Optional filter for options and group names.
    godot::LineEdit *search = nullptr;
    /// Scrolling viewport for large option lists.
    godot::ScrollContainer *scroll = nullptr;
    /// Parent of rendered option rows and group headings.
    godot::VBoxContainer *list = nullptr;
    /// Empty result message for the current filter.
    godot::Label *empty = nullptr;
    /// Current value display used above grouped search results.
    godot::Label *current_value = nullptr;
    /// Container for the current value display and its descriptor.
    godot::HBoxContainer *current_row = nullptr;
    /// Items with stable IDs, retained across search and dismissal.
    std::vector<PickerOption> options;
    /// Selectable row per item, indexed with options.
    std::vector<godot::Button *> rows;
    /// Current selection marks indexed with the option rows.
    std::vector<godot::Control *> checks;
    /// Disclosure buttons in order of first group appearance.
    std::vector<godot::Button *> group_buttons;
    /// One group label for each disclosure button.
    std::vector<godot::String> group_names;
    /// Whether each group's children are visible when filtering is inactive.
    std::vector<godot::String> collapsed_groups;
    /// The option ID presented by the trigger.
    godot::StringName selection;
    /// Prompt displayed when no item is selected.
    godot::String placeholder;
    /// Callback that receives the committed ID or index in compact mode.
    godot::Callable on_chosen;
    /// Presentation and selection semantics for this instance.
    PickerMode mode = PickerMode::RICH;
    /// Whether finite options use the compact Select or command menu metrics.
    SelectStyle select_style = SelectStyle::STANDARD;
    /// Whether the next open needs to instantiate the supplied option data.
    bool dirty = true;
    /// Whether the trigger's minimum height is driven by selected rich content.
    bool rich_trigger = false;
    /// Maximum popup width in logical pixels. Zero follows the standard trigger width.
    int popup_width_limit = 0;
    /// Minimum popup width in logical pixels, independent of a narrow trigger.
    int popup_width_minimum = 0;
    /// Available height on the chosen side of the trigger for the current opening.
    int searchable_height_limit = 0;
    /// Keep the bottom edge next to the trigger when a searchable picker opens above it.
    bool searchable_above_trigger = false;
    /// Current availability of the anchor.
    FieldState availability = FieldState::EDITABLE;

    /// Update option visibility from the filter and collapsed groups.
    void filter(const godot::String &text);
    /// Measure the visible results and the search header, with a bounded scroll area.
    int searchable_height() const;
    /// Adjust the open popup in place without dropping search or row focus.
    void fit_searchable_popup();
    /// Create lazy option controls on first open or after an option list replacement.
    void build_rows();
    /// Destroy previously presented options, preserving the popup shell.
    void clear_rows();
    /// Update trigger and selected row presentation.
    void refresh_selection();
    /// Reserve an inline marker only when the selected option supplies one.
    void show_trigger_marker(const godot::Ref<godot::Texture2D> &marker);
    /// Toggle a group without altering the selection.
    void toggle_group(godot::String group);
    /// Open the popup, position it at the anchor and focus a valid target.
    void open();
    /// Restore focus to the anchor after dismissal.
    void restore_focus();
    /// Commit an option and close the popup.
    void choose(int index);
    /// Show a matching option when Enter is pressed in the search field.
    void submit_search(const godot::String &text);

protected:
    /// This internal component exposes no script bindings.
    static void _bind_methods() {}

public:
    /// Build an anchored selector with the requested presentation.
    void configure(const godot::String &prompt, const godot::Callable &chosen, PickerMode presentation,
            SelectStyle style = SelectStyle::STANDARD);
    /// Replace the complete option list and adopt any detached custom content.
    void set_options(std::vector<PickerOption> items);
    /// Show the supplied selection without notifying the caller.
    void set_selected(const godot::StringName &id);
    /// Enable editing, set read-only, or disable the trigger.
    void set_state(FieldState state);
    /// Keep a compact popup even when the containing editor row is wide.
    void set_popup_width_limit(int logical_width);
    /// Give option text room to load at its full width while preserving a narrow trigger.
    void set_popup_minimum_width(int logical_width);
    /// Name the content filtered by a searchable picker.
    void set_search_placeholder(const godot::String &text);
    /// Return the anchor button for compact presentation overrides.
    godot::Button *get_trigger() const;
    /// Return the selected stable ID, or an empty name when no value is selected.
    godot::StringName get_selected() const;
    /// Replace plain labels and set the current item index without notifying the caller.
    void set_items(const godot::PackedStringArray &items, int selected = -1);
    /// Set the current item index without notifying the caller.
    void select(int selected);
    /// Return the current item index, or -1 when there is no selected item.
    int get_selected_index() const;
    /// Display the provided text when no item is selected.
    void set_placeholder(const godot::String &prompt);
    /// Enable or disable one indexed item without changing the committed selection.
    void set_item_disabled(int index, bool disabled);
};

/// Attach an anchored picker. Chosen receives the stable option ID.
Picker *picker(godot::Node *owner, const godot::String &placeholder, const godot::Callable &chosen = {}, bool searchable = false);

/// Attach a compact Select for plain labels. Changed receives the item index.
Picker *select(godot::Node *owner, const godot::PackedStringArray &options, int selected = -1,
        const godot::Callable &changed = {}, FieldState state = FieldState::EDITABLE,
        SelectStyle style = SelectStyle::STANDARD);

/// Attach a toggle button and optional content, for a collapsible section.
godot::Button *disclosure(godot::Node *owner, const godot::String &name, godot::Control *content,
        bool expanded = false, const godot::String &help = {});

/// Attach a tab container. The caller adds Control children as pages.
godot::TabContainer *tabs(godot::Node *owner);

/// Attach a menu trigger. The caller configures its native popup items.
godot::MenuButton *menu(godot::Node *owner, const godot::String &name, const godot::String &help = {});

/// A modal in the same viewport as its editor workbench.
class EditorModal final : public godot::Control {
    GDCLASS(EditorModal, godot::Control)

    godot::Control *workbench = nullptr; ///< The owner's temporarily blocked content.
    godot::PanelContainer *card = nullptr; ///< Centered prompt and actions.
    godot::PanelContainer *scrim = nullptr; ///< Dims the parent workbench underneath the card.
    godot::Label *heading = nullptr; ///< Current dialog title.
    godot::Label *message = nullptr; ///< Optional explanatory copy.
    godot::Label *validation_message = nullptr; ///< Inline guidance from a rejected submission.
    godot::VBoxContainer *form = nullptr; ///< Authored controls kept alive between opens.
    godot::Button *cancel_button = nullptr; ///< Safe default and return target.
    godot::Button *confirm_button = nullptr; ///< Primary action for the question or form.
    godot::Button *alternate_button = nullptr; ///< Optional secondary action in a multi-choice prompt.
    godot::Callable on_confirmed; ///< Reports an accepted action to the owner.
    godot::Callable on_canceled; ///< Reports dismissal to the owner.
    godot::Callable on_alternate; ///< Reports a secondary action to the owner.
    godot::Callable validator; ///< An optional preflight that keeps invalid forms open.
    godot::ObjectID prior_control; ///< The background control that owned focus before opening.
    FocusBehaviorRecursive prior_focus = FOCUS_BEHAVIOR_INHERITED; ///< Original workbench focus behavior.
    MouseBehaviorRecursive prior_mouse = MOUSE_BEHAVIOR_INHERITED; ///< Original workbench mouse behavior.
    bool active = false; ///< Whether this invocation is awaiting an answer.
    bool destructive = false; ///< Whether this dialog uses the destructive action treatment.
    int preferred_width = 480; ///< Desired card width in logical pixels.

    /// Fit the centered card to the owner's available logical bounds.
    void fit_card();
    /// Dismiss a pending question when its owner hides.
    void owner_visibility_changed();
    /// Reply once and restore the owner's input before delivering the callback.
    void finish(const godot::Callable &reply, bool restore_focus);
    /// Release the modal before reporting the secondary action.
    void choose_alternate();
    /// Confirm from a single-line text field that owns an Enter submission.
    void text_submitted(const godot::String &text);
    /// Keep sequential focus movement among the modal's currently visible controls.
    void move_focus(bool backwards);

protected:
    /// This internal composition exposes no script bindings.
    static void _bind_methods() {}

public:
    /// Handle Back or Escape before either action receives the key.
    void _input(const godot::Ref<godot::InputEvent> &event) override;
    /// Build the modal and connect the owner's two completion callbacks.
    void configure(godot::Node *owner, godot::Control *background, const godot::Callable &confirmed,
            const godot::Callable &canceled);
    /// Ask the current question with optional additional consequence text.
    void open(const godot::String &prompt, const godot::String &description = {},
            godot::Control *initial_focus = nullptr, int logical_width = 480);
    /// Access the vertical form slot where the owner attaches existing controls.
    godot::VBoxContainer *body() const;
    /// Change the text of the available primary action.
    void set_confirm_text(const godot::String &text);
    /// Enable confirmation once the form has a valid selection.
    void set_confirm_enabled(bool enabled);
    /// Use the destructive action color for the primary button.
    void set_destructive(bool destructive);
    /// Hide Cancel for a one-button information dialog.
    void set_cancel_visible(bool visible);
    /// Add or clear a secondary action between the Cancel and primary buttons.
    void set_alternate_action(const godot::String &text, const godot::Callable &action);
    /// Run a boolean validation before closing an accepted form.
    void set_validator(const godot::Callable &validate);
    /// Show or clear feedback while the current form remains open.
    void set_validation_message(const godot::String &text);
    /// Treat Enter in a single-line field as an attempt to confirm.
    void register_text_enter(godot::LineEdit *entry);
    /// Send the enabled primary action after its validator accepts the form.
    void confirm();
    /// Dismiss the prompt and report cancellation once.
    void cancel();
    /// Close without notifying the owner, for deliberate programmatic dismissal.
    void close();
    /// Read whether the owner's command routing is currently blocked by a prompt.
    bool is_open() const;
};

/// Attach a reusable destructive confirmation to one editor workbench.
EditorModal *removal_confirmation(godot::Window *owner, godot::Control *background,
        const godot::Callable &confirmed, const godot::Callable &canceled = {});

/// Attach a shared modal to one editor workbench.
EditorModal *editor_modal(godot::Node *owner, godot::Control *background,
        const godot::Callable &confirmed, const godot::Callable &canceled = {});

/// Style an attached stock file dialog without changing its file operations or native footer layout.
void style_file_dialog(godot::FileDialog *dialog);

/// Attach accessible help to a control, wrapping long text at word boundaries.
void tooltip(godot::Control *control, const godot::String &help);

/// Attach a determinate or indeterminate progress display with an Inter value label.
godot::ProgressBar *progress(godot::Node *owner, const godot::String &name, double percent, bool indeterminate = false);

/// Register internal stock-control compositions at editor initialization.
void register_editor_controls();

}

#endif
