// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/visual_script.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace godot {
class Button;
class Control;
class HBoxContainer;
class InputEvent;
class Label;
class LineEdit;
class PopupMenu;
class Tree;
class TreeItem;
class VBoxContainer;
}

namespace crimson::visual_scripting {

/// Check whether an editor transaction would invalidate a previously valid connection.
bool visual_script_edit_breaks_wires(const godot::Dictionary &before, const godot::Dictionary &after,
        const godot::StringName &base);

/// Owns authoring controls while the graph window owns document mutations and undo history.
class VisualScriptPanels final : public godot::Node {
    GDCLASS(VisualScriptPanels, godot::Node)

public:
    /// Commit a complete document in one undo step.
    using Commit = std::function<void(const godot::String &, const godot::Dictionary &)>;
    /// Commit collection row identities alongside the authored document in the same undo step.
    using CommitCollection = std::function<void(const godot::String &, const godot::Dictionary &,
            const godot::Callable &, const godot::Callable &)>;
    /// Variable changes whose consequences need confirmation before commit.
    enum class VariableEdit { TYPE, RENAME_EXPORTED, HIDE_EXPORTED, CUT_EXPORTED, DELETE_EXPORTED };
    /// Ask the graph window to confirm a variable edit and its consequences.
    using ConfirmVariableEdit = std::function<void(VariableEdit, const godot::Dictionary &, const godot::String &, const godot::Callable &)>;
    /// Switch the canvas to a declaration's graph. Zero opens the Event Graph.
    using OpenGraph = std::function<void(int64_t)>;
    /// Insert a node descriptor into the current canvas at its next available position.
    using InsertNode = std::function<void(const godot::Dictionary &)>;
    /// Open the code editor for the selected node and restore this control on cancel.
    using EditCode = std::function<void(int64_t, godot::Control *)>;

    /// Create the declarations in the graph workspace sidebar.
    void build_left(godot::Control *container, Commit commit, OpenGraph open_graph, InsertNode insert_node, EditCode edit_code);
    /// Create the scrollable Details panel.
    void build_right(godot::Control *container);
    /// Dismiss popups owned by the declarations or Details before this workspace is covered or hidden.
    void close_popups();
    /// Supply the owner modal used for variable changes that reset values or remove links.
    void set_variable_edit_confirmation(ConfirmVariableEdit confirm);
    /// Supply the graph window's transaction path for collection row edits.
    void set_collection_commit(CommitCollection commit);
    /// Invalidate running Details drafts when the author explicitly reloads the script.
    void discard_collection_drafts();
    /// Refresh the view from the active document and graph.
    void refresh(const godot::Dictionary &document, int64_t graph_id, const godot::Ref<VisualScript> &script);
    /// Keep declaration edits based on the latest graph without replacing active controls.
    void update_document(const godot::Dictionary &next_document);
    /// Read the current snapshot for pointer previews without resaving the graph.
    const godot::Dictionary &document_snapshot() const { return document; }
    /// Show the settings of the selected canvas node.
    void select_graph_node(const godot::Dictionary &node);
    /// Prepare a named event declaration for the window to commit together with its entry node.
    godot::Dictionary new_custom_event(const godot::Dictionary &source, int64_t &event_id) const;
    /// Select a My Script declaration, including one awaiting the window's next refresh.
    void show_declaration(const godot::StringName &section, int64_t id);
    /// Read the explicitly selected My Script declaration and its local owner, if any.
    godot::Dictionary selected_symbol() const;
    /// Resolve a node dragged from Godot's Scene dock against the current script attachment.
    godot::Dictionary scene_dock_reference(const godot::Dictionary &drag) const;

    /// Route matching row drops from the real pointer coordinates in the graph window.
    void _input(const godot::Ref<godot::InputEvent> &event) override;

protected:
    /// The panels are private to the editor and expose no script bindings.
    static void _bind_methods() {}

    /// Clear insertion feedback when the native drag ends or the window loses pointer ownership.
    void _notification(int what);

private:
    /// Identifies a declaration within the current document or function body.
    struct DeclarationAddress {
        /// Declaration group in the document or current function.
        godot::StringName section;
        /// Stored declaration ID, or zero when a group has no selected row.
        int64_t id = 0;
        /// Compare the selected declaration when determining whether a draft can be restored.
        bool operator==(const DeclarationAddress &other) const { return section == other.section && id == other.id; }
    };

    /// Actions shared by the declaration context menu and its keyboard shortcuts.
    enum class DeclarationCommand { RENAME, CUT, COPY, DUPLICATE, DELETE_DECLARATION, PASTE };

    /// Active script for resolving scene references.
    godot::Ref<VisualScript> script;
    /// Most recent immutable graph document snapshot.
    godot::Dictionary document;
    /// Last selected canvas node ID, or zero when Details is showing a declaration.
    int64_t graph_node_id = 0;
    /// Current canvas declaration ID, with zero for the Event Graph.
    int64_t graph_id = 0;
    /// Selected declaration section, or empty if Details shows another kind of item.
    godot::StringName section;
    /// ID of the selected declaration.
    int64_t declaration_id = 0;
    /// Remember the last unambiguous owner selected in the stock Scene dock while dragging its targets.
    godot::ObjectID remembered_scene_owner;
    /// Hierarchy of declarations in the current script.
    godot::Tree *declarations = nullptr;
    /// Insertion feedback for the declaration tree.
    godot::Control *declaration_drag_rail = nullptr;
    /// Context actions for an authored declaration or its group.
    godot::PopupMenu *declaration_menu = nullptr;
    /// Deep-copied declaration retained after Cut or Copy.
    godot::Dictionary declaration_clipboard;
    /// Original group and ID, used to keep pasted references in the correct scope.
    DeclarationAddress clipboard_address;
    /// Function owning a copied local or zero for script-wide declarations.
    int64_t clipboard_owner = 0;
    /// Script owning the copied declaration and its dependencies.
    uint64_t clipboard_script = 0;
    /// Definitions used by the copied declaration that must stay live until Paste.
    std::vector<DeclarationAddress> clipboard_dependencies;
    /// A dependency was removed after Copy or Cut and its ID may have been reassigned.
    bool clipboard_stale = false;
    /// Scrollable body of the Details panel.
    godot::VBoxContainer *details = nullptr;
    /// Viewport clip and insertion feedback for rows inside Details.
    godot::Control *details_scroll = nullptr;
    /// Insertion feedback for ordered collection and enum members.
    godot::Control *details_drag_rail = nullptr;
    /// Inline feedback for a rejected Details edit.
    godot::Label *details_feedback = nullptr;
    /// The exact accepted graph snapshot whose row controls already reflect the edit.
    godot::Dictionary details_applied_document;
    /// Declaration that owns the still-live row controls for that snapshot.
    DeclarationAddress details_applied_address;
    /// Scope of the rendered rows. Drafts belong to this script, graph and selected declaration.
    DeclarationAddress draft_address;
    /// Script owning the currently rendered Details fields.
    uint64_t draft_script = 0;
    /// Graph owning the currently rendered Details fields.
    int64_t draft_graph = 0;
    /// Changes when the selected Details scope changes, invalidating history callbacks for that view.
    int64_t draft_scope = 0;
    /// Unique identity source for rows created throughout the window's lifetime.
    int64_t next_collection_row = 1;
    /// Previous field values and pending text, used only during one Details rebuild.
    godot::Dictionary collection_drafts;
    /// Authoritative replacement row order from an accepted collection transaction.
    godot::Dictionary collection_orders;
    /// Pending input for each stable enum member ID, used only during one Details rebuild.
    godot::Dictionary enum_drafts;
    /// Prevent another stale control from publishing while Details awaits the current collection redraw.
    bool collection_refresh_pending = false;
    /// Signature pin groups folded during the current script editing session.
    std::set<godot::String> folded_signature_pins;
    /// Details sections explicitly toggled during the current script editing session.
    std::map<godot::StringName, bool> details_section_expansion;
    /// Commit callback owned by the graph window.
    Commit on_commit;
    /// Add a row-identity update to the document's own Undo/Redo action.
    CommitCollection on_collection_commit;
    /// Stages a complete variable change until confirmed.
    ConfirmVariableEdit confirm_variable_edit;
    /// Navigation callback owned by the graph window.
    OpenGraph on_open_graph;
    /// Insertion callback owned by the graph window.
    InsertNode on_insert_node;
    /// Code editor callback owned by the graph window.
    EditCode on_edit_code;
    /// Prevent selection signals during a tree rebuild from changing the current Details context.
    bool refreshing = false;

    /// Resolve the node that owns the active script in the current editor scene.
    godot::Node *script_owner(godot::Node *scene_root) const;
    /// Track the attachment when the Scene dock selects a node running the active script.
    void remember_scene_owner();
    /// Rebuild declaration groups while retaining the current selection.
    void rebuild_declarations();
    /// Resolve the declaration selected in My Script or referenced by a selected variable node.
    DeclarationAddress selected_declaration_address() const;
    /// Read the record addressed by the current Details selection.
    godot::Dictionary selected_declaration() const;
    /// Rebuild the controls for the current Details selection.
    void rebuild_details();
    /// Add a disclosure to Details and return its body and optional heading actions.
    godot::VBoxContainer *details_section(const godot::StringName &id, const godot::String &title,
            const godot::String &help, bool initially_expanded = false, godot::HBoxContainer **actions = nullptr);
    /// Preserve the author's disclosure choice when the selected item's controls rebuild.
    void set_details_section_expanded(bool expanded, const godot::StringName &id);
    /// Construct a collection editor with session-only row IDs and recover its surviving drafts.
    void draw_collection(godot::Node *parent, const godot::Dictionary &pin, const godot::Variant &value,
            const godot::Callable &commit, const godot::StringName &key, bool optional, const godot::Array &key_order);
    /// Stage the next row order only if the owning scope and row ordering still match.
    void apply_collection_order(const godot::PackedInt64Array &from, const godot::PackedInt64Array &to,
            const godot::StringName &key, int64_t scope);
    /// Fold or unfold a signature pin's fields and remember that choice until the script changes.
    void toggle_signature_pin(bool folded, const godot::String &key, godot::Control *fields, godot::Button *trigger);
    /// Display a declaration's name, type, default or signature.
    void draw_declaration(const godot::Dictionary &record, const godot::StringName &declaration_section);
    /// Show Godot Inspector presentation settings for an exported instance variable.
    void draw_variable_inspector(const godot::Dictionary &record, const godot::Dictionary &pin);
    /// Display settings supported by the selected canvas node.
    void draw_graph_node(const godot::Dictionary &node);
    /// Display the stable typed data pins of a Custom Script node.
    void draw_custom_script(const godot::Dictionary &node);
    /// Set the card's display title or clear the override on a blank submission.
    void rename_custom_script(const godot::String &title, int64_t node_id, int64_t body_id);
    /// Open the selected Custom Script node's source editor.
    void edit_selected_custom_code(godot::Control *trigger);
    /// Resolve the Custom Script node currently selected in Details.
    godot::Dictionary selected_custom_script() const;
    /// Add a typed data pin with a stable ID and a separate source identifier.
    void add_custom_pin(const godot::StringName &direction);
    /// Rename the variable exposed to GDScript without changing its wire ID.
    void rename_custom_pin(const godot::String &label, const godot::StringName &direction, const godot::StringName &id);
    /// Retype a Custom Script data pin through the shared type picker.
    void change_custom_pin_type(const godot::Dictionary &choice, const godot::StringName &direction,
            const godot::StringName &id);
    /// Remove the selected Custom Script data pin if no wire depends on it.
    void remove_custom_pin(const godot::StringName &direction, const godot::StringName &id);
    /// Validate and commit one pin edit through the native graph wire checks.
    void edit_custom_pin(const godot::StringName &direction, const godot::StringName &id,
            const godot::Dictionary &replacement);
    /// Show the native and scripted signal actions for a selected scene reference.
    void draw_scene_reference_signals(const godot::Dictionary &node);
    /// Select a declaration in the sidebar.
    void select_declaration();
    /// Activate the declaration or graph selected in the tree.
    void activate_declaration();
    /// Route editor shortcuts and right-clicks while the declaration tree receives input.
    void declaration_input(const godot::Ref<godot::InputEvent> &event);
    /// Build and open the context menu for the row under the pointer.
    bool open_declaration_menu(const godot::Vector2 &point);
    /// Dispatch a declaration menu item using the selected row.
    void declaration_menu_command(int64_t command);
    /// Run a shortcut or menu action against the current explicit tree selection.
    bool run_declaration_command(DeclarationCommand command);
    /// Focus the declaration's Name editor so the author can type a replacement.
    void focus_declaration_name();
    /// Check that the clipboard belongs to the current script and local function, when applicable.
    bool can_paste_declaration(const godot::StringName &group) const;
    /// Remember every declaration the copied record refers to outside its own body.
    void copy_declaration(const DeclarationAddress &address, const godot::Dictionary &record);
    /// Invalidate a saved snapshot after one of its dependencies is removed.
    void check_clipboard_dependencies(const godot::Dictionary &updated);
    /// Clone the record and select the copy, adapting stored self-references to its new ID.
    void duplicate_declaration(const godot::StringName &group, const godot::Dictionary &record, const godot::String &action);
    /// Remove a declaration and its graph references in one undoable edit.
    void delete_declaration(const DeclarationAddress &address, const godot::Dictionary &record, bool cut);
    /// Apply clipboard and selection changes after a declaration removal is confirmed.
    void finish_declaration_removal(bool cut, const godot::Dictionary &record, const godot::StringName &group, int64_t id);
    /// Supply Get/Set, Call, or event options when a declaration row is dragged.
    godot::Variant drag_declaration(const godot::Vector2 &point);
    /// Accept reordering between distinct declarations in the same group and script.
    bool can_reorder_declaration(const godot::Vector2 &point, const godot::Variant &payload);
    /// Move the declaration without changing its identity or references.
    void reorder_declaration(const godot::Vector2 &point, const godot::Variant &payload);
    /// Handle a declaration group creation button or a variable's Inspector toggle.
    void press_declaration_button(godot::TreeItem *item, int64_t column, int64_t id, int64_t mouse_button);
    /// Create and select a declaration, optionally with an existing enum as its type.
    void create_declaration(const godot::StringName &section, const godot::StringName &enum_type = {});
    /// Validate and commit a new declaration name.
    void rename_declaration(const godot::String &name);
    /// Save text shown when authors hover this declaration or a node that references it.
    void edit_declaration_description(const godot::String &description);
    /// Set a display group or a numeric suffix without changing the exported property name.
    void edit_variable_inspector_text(const godot::String &text, const godot::StringName &field);
    /// Add a valid initial interval or remove the existing Inspector range.
    void toggle_variable_inspector_range(bool enabled);
    /// Parse and save the three edited range values together.
    void edit_variable_inspector_range(const godot::String &submitted);
    /// Choose whether the numeric interval has a slider as well as a text field.
    void toggle_variable_inspector_slider(bool enabled);
    /// Commit optional Inspector metadata with the selected variable's Undo history.
    void commit_variable_inspector(const godot::Dictionary &inspector, bool keep_details = false);
    /// Save a supported presentation field from a function or macro's Settings section.
    void edit_function_setting(const godot::String &value, const godot::StringName &field);
    /// Edit a declaration's typed pin or default.
    void change_variable_type(const godot::Dictionary &choice);
    /// Switch the selected variable between a single value, Array, or Dictionary.
    void change_variable_container(int64_t container);
    /// Change the selected Dictionary variable's value type, retaining its key type.
    void change_variable_dictionary_value(const godot::Dictionary &choice);
    /// Commit a text-backed variable default.
    void change_variable_value(const godot::String &value);
    /// Commit one vector axis while validating range before Godot updates the value.
    void change_variable_axis(const godot::String &value, int32_t axis);
    /// Commit a Boolean variable default.
    void change_variable_bool(bool value);
    /// Expose the addressed instance variable in the attached node's Inspector.
    void change_variable_export(bool enabled, int64_t variable_id);
    /// Commit a new field value on the selected variable declaration.
    bool commit_variable_value(const godot::Variant &value);
    /// Display and edit the named integer values of an authored enum.
    void draw_enum(const godot::Dictionary &record);
    /// Append a distinct enum value and a stable case ID.
    void add_enum_entry();
    /// Change the name of one enum value without changing its case ID.
    void rename_enum_entry(const godot::String &name, int64_t entry_id);
    /// Update the integer matched by an enum value.
    void change_enum_entry_value(const godot::String &value, int64_t entry_id);
    /// Remove one enum value after checking existing switch connections.
    void remove_enum_entry(int64_t entry_id);
    /// Reorder enum members of the still-visible declaration without changing their IDs or values.
    void move_enum_entry(int64_t from, int64_t to, int64_t enum_id, int64_t list_id);
    /// Remove an enum that has no dependent variable, signature or graph node.
    void remove_enum();
    /// Publish an enum member edit through the shared switch refresh and undo path.
    void edit_enum_entry(int64_t entry_id, const godot::Dictionary &replacement, const char *submitted_field = nullptr);
    /// Update an array or dictionary variable default and its visible dictionary row order.
    bool edit_variable_collection(const godot::Variant &value, const godot::Array &key_order,
            const godot::Callable &redo, const godot::Callable &undo);
    /// Update a collection input default and its visible dictionary row order.
    bool edit_signature_collection(const godot::Variant &value, const godot::Array &key_order,
            const godot::Callable &redo, const godot::Callable &undo,
            const godot::StringName &direction, const godot::StringName &pin_id);
    /// Add one signature pin, giving it a stable identifier independent of its label.
    void add_signature_pin(const godot::StringName &direction, bool execution);
    /// Insert the declaration's default call or event action.
    void insert_selected_declaration(bool alternative);
    /// Open the function or macro selected in Details.
    void open_selected_declaration();
    /// Open the graph referred to by the selected call.
    void open_referenced_declaration(int64_t id);
    /// Edit the selected signature pin's visible name.
    void rename_signature_pin(const godot::String &name, const godot::StringName &direction, const godot::StringName &pin_id);
    /// Change the type selected for a signature pin.
    void change_signature_type(const godot::Dictionary &choice,
            const godot::StringName &direction, const godot::StringName &pin_id);
    /// Edit a text-backed input default.
    void change_signature_default(const godot::String &value, const godot::StringName &direction, const godot::StringName &pin_id);
    /// Remove an unwired signature pin.
    void remove_signature_pin(const godot::StringName &direction, const godot::StringName &pin_id);
    /// Apply one signature pin change to the selected declaration.
    bool edit_signature_pin(const godot::StringName &direction, const godot::StringName &pin_id,
            const godot::Dictionary &replacement, bool keep_details = false,
            const godot::Callable &redo = {}, const godot::Callable &undo = {});
    /// Draw one editable direction of a signature.
    void draw_signature(const godot::Dictionary &record, const godot::StringName &direction, bool macro);
    /// Validate and save one replacement declaration.
    bool replace_declaration(const godot::String &title, const godot::Dictionary &record, bool keep_details = false,
            const godot::Callable &redo = {}, const godot::Callable &undo = {});
    /// Validate and save one replacement declaration without changing the current selection.
    bool replace_declaration(const DeclarationAddress &address, const godot::String &title,
            const godot::Dictionary &record, bool keep_details = false, const godot::Callable &redo = {},
            const godot::Callable &undo = {});
    /// Stage a change that would remove stored Inspector overrides.
    void confirm_export_edit(const DeclarationAddress &address, VariableEdit edit, const godot::Dictionary &record);
    /// Rebuild a variable default and collect only wires invalidated by the new type.
    void prepare_variable_type(const godot::Dictionary &pin);
    /// Edit the selected Switch node's typed cases.
    void change_switch_cases(const godot::String &values);
    /// Change one field of the selected canvas node, or remove it for a nil value.
    void replace_node_field(const godot::String &title, const godot::StringName &field, const godot::Variant &value);
    /// Insert the selected source's native signal into the Event Graph.
    void insert_native_signal(const godot::Dictionary &node);
    /// Report an unsuccessful edit without altering the graph.
    void report(const godot::String &message);
};

/// Register the internal panel controller before any graph windows are created.
void register_visual_script_panels();

} // namespace crimson::visual_scripting

#endif
