// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/visual_script.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace godot {
class Button;
class Control;
class CheckBox;
class GraphEdit;
class GraphNode;
class InputEvent;
class InputEventKey;
class GraphElement;
class HBoxContainer;
class Label;
class LineEdit;
class PanelContainer;
class PopupPanel;
class PopupMenu;
class Resource;
class SceneTreeTimer;
class TextureRect;
class Tree;
class TreeItem;
class UndoRedo;
class Window;
}

namespace crimson::editor_ui {
class EditorModal;
}

namespace crimson::visual_scripting {

class VisualScriptPanels;
class VisualScriptEditorDebugger;
class VisualScriptCustomCode;

/// The Godot main-screen workspace for one attached or saved visual script.
class VisualScriptWindow final : public godot::Control {
    GDCLASS(VisualScriptWindow, godot::Control)

    friend class VisualScriptEditorDebugger;

    /// Exclusive stock code editor for the current Custom Script node draft.
    VisualScriptCustomCode *custom_code = nullptr;
    /// Open a Custom Script code draft for the exact node in the active graph.
    void edit_custom_code(int64_t node_id, godot::Control *trigger);

    /// Resource and node currently paused in a running game.
    godot::Dictionary debug_hit;

    /// Run controls at the top of the graph workspace.
    godot::Control *debug_toolbar = nullptr;
    /// The lower right pane for the pause snapshot.
    godot::Control *debug_pane = nullptr;
    /// Running or Paused badge in the run controls.
    godot::Label *debug_status = nullptr;
    /// The node that is about to run, or the next action while the game is running.
    godot::Label *debug_context = nullptr;
    /// Plain-language location of the pause within a graph and an instance.
    godot::Label *debug_location = nullptr;
    /// The read-only inspector for the current pause.
    godot::Tree *debug_values = nullptr;
    /// Display values held only for the current pause.
    godot::Dictionary debug_snapshot;
    /// Index displayed values by graph, node, pin direction and stable pin name.
    std::map<std::tuple<int64_t, int64_t, bool, godot::StringName>, godot::Dictionary> debug_pin_samples;
    /// Keep manually expanded inspector sections open across pauses or watch edits.
    std::map<godot::String, bool> debug_section_collapsed;
    /// The available commands in display and tab order.
    std::vector<godot::Button *> debug_actions;
    /// Whether inspection data from a paused session is current.
    bool debug_paused = false;
    /// Whether the paused session has finished its native stack exchange.
    bool debug_commands_ready = false;

    /// Build the command strip and the resizable lower right value inspector.
    void build_debug_controls(godot::Node *workspace, godot::Control *right_sidebar);
    /// Update control availability when the game starts, stops, pauses, or resumes.
    void debug_set_run_state(bool running, bool paused, bool commands_ready);
    /// Present bounded, display-ready values from the active paused graph.
    void debug_apply_snapshot(const godot::Dictionary &snapshot);
    /// Route a graph control command to the debugger session owning this pause.
    void debug_command(const godot::StringName &action);
    /// Handle graph-local step controls while the game is paused.
    bool debug_shortcut(const godot::Ref<godot::InputEventKey> &event);

    /// Show the active visual breakpoint and center its node, or clear it when play resumes.
    void debug_focus_hit(const godot::Dictionary &hit);
    /// Drop personal breakpoint entries as their nodes are removed from the document.
    void debug_prune_breakpoints();

    /// A connection endpoint retained while the add-node palette is open.
    struct PendingPin {
        /// The graph node ID.
        int64_t node = 0;
        /// The stable pin ID.
        godot::StringName pin;
        /// Type and optional object class of the originating pin.
        godot::Dictionary descriptor;
        /// Whether the pin starts an outgoing connection.
        bool output = false;
        /// Optional current type used by the picker, separate from the pin's native contract.
        std::optional<godot::Dictionary> presentation = {};

        /// Prefer the current displayed type when offering an action for this pin.
        const godot::Dictionary &picker_descriptor() const { return presentation ? *presentation : descriptor; }
    };

    /// Locate watches saved for one script, including watches in its callable graphs.
    godot::Array debug_watch_records(const godot::String &path) const;
    /// Update a personal watch by stable script, graph, node, and pin address.
    void debug_watch_set(const godot::Dictionary &address, bool enabled);
    /// Drop watches whose authored pin can no longer be matched.
    void debug_watch_erase_missing(const godot::String &path, const godot::Dictionary &document);
    /// Build the current script and graph address for a clicked or hovered data pin.
    godot::Dictionary debug_pin_address(const PendingPin &pin) const;
    /// Look up a captured value at the exact address, or describe why it is unavailable.
    godot::Dictionary debug_pin_value(const godot::Dictionary &address) const;
    /// Describe a paused pin without reading live objects or executing graph nodes.
    godot::String debug_pin_tooltip(const PendingPin &pin) const;
    /// Detect a socket or its paired label without changing the native wire hit targets.
    PendingPin debug_pin_hit(godot::Vector2 point, godot::GraphNode *top_node) const;
    /// Find the frontmost node card at the canvas point.
    godot::GraphNode *node_at_canvas_point(godot::Vector2 point) const;
    /// Highlight one typed socket from a canvas position, or clear the highlight.
    void update_pin_hover(std::optional<godot::Vector2> point);
    /// Make native socket hit zones and their labels show the same value tooltip.
    godot::String debug_canvas_tooltip(godot::Vector2 point) const;
    /// Refresh existing pin labels when the paused state changes, without rebuilding the graph.
    void debug_refresh_pin_labels();
    /// Remove a watch using the native action button on its debugger row.
    void debug_watch_clicked(godot::TreeItem *item, int64_t column, int64_t button_id, int64_t mouse_button);

    /// Commands that rearrange the current selection without changing its behavior.
    enum class LayoutCommand { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT, ALIGN_TOP, ALIGN_MIDDLE, ALIGN_BOTTOM,
        DISTRIBUTE_HORIZONTAL, DISTRIBUTE_VERTICAL, SNAP, ARRANGE, STRAIGHTEN };

    /// A Godot class in the native node palette.
    struct NativeClass {
        /// The class's immediate descendant classes, sorted by name.
        std::vector<godot::StringName> children;
        /// Whether this class can be constructed at runtime.
        bool constructible = false;
    };

    /// A camera and selection snapshot in one graph.
    struct GraphLocation {
        /// Stable graph definition ID; zero is the Event Graph.
        int64_t graph = 0;
        /// Camera center in graph coordinates, independent of window size.
        godot::Vector2 center;
        /// Canvas magnification.
        float zoom = 1.0f;
        /// Selected graph nodes.
        godot::PackedInt64Array selection;
    };

    /// The identity used to compare declaration references across graphs.
    struct SymbolAddress {
        /// Symbol table within the script or function body.
        godot::StringName section;
        /// Stable declaration ID in that table.
        int64_t id = 0;
        /// Owning graph for local variables.
        int64_t owner = 0;
    };

    /// The resource currently being edited.
    godot::Ref<VisualScript> script;
    /// Select the Godot main screen that owns this workspace.
    godot::Callable reveal_workspace;
    /// Authoring controls visible after opening a script.
    godot::Control *workbench = nullptr;
    /// Instructions shown until a script is opened from the FileSystem or Scene dock.
    godot::Control *empty_workspace = nullptr;
    /// The resource requested while the previous one has unsaved edits.
    godot::Ref<VisualScript> next_script;
    /// The graph exactly as opened or last saved.
    godot::Dictionary saved_graph;
    /// Content hash last read from or written to the active script file.
    godot::String saved_disk_hash;
    /// Delay writing until the user finishes a short burst of graph edits.
    godot::Ref<godot::SceneTreeTimer> autosave_timer;
    /// A save failure pauses automatic attempts until an explicit retry or reload.
    godot::String autosave_error;
    /// Undo history scoped to the active graph resource.
    godot::UndoRedo *history = nullptr;
    /// The stock graph canvas.
    godot::GraphEdit *canvas = nullptr;
    /// Scene, script outline, and selected-item details in the side panes.
    VisualScriptPanels *panels = nullptr;
    /// Stable definition ID of the canvas graph; zero is the event graph.
    int64_t active_graph = 0;
    /// Map stable node IDs to live editor controls.
    std::map<int64_t, godot::GraphNode *> widgets;
    /// The card receiving the pointer's current pin highlight.
    int64_t pin_hover_node = 0;
    /// Native comment frames indexed separately from executable graph nodes.
    std::map<int64_t, godot::GraphElement *> comment_frames;
    /// Nodes to select when a newly pasted subgraph has built its native widgets.
    godot::PackedInt64Array pending_canvas_selection;
    /// Keep a local copy for editor sessions whose display driver has no OS clipboard.
    godot::String graph_clipboard;
    /// Own the captured paste only while the user is reviewing its conflicts.
    struct PendingDocumentPaste {
        /// Script open when the clipboard was first pasted.
        godot::Ref<VisualScript> owner;
        /// Destination snapshot used to plan the choices.
        godot::Dictionary destination;
        /// Validated clipboard document held independently of later clipboard updates.
        godot::Dictionary source;
        /// Destination inheritance base at the time of paste.
        godot::StringName base;
        /// Destination graph at the time of paste.
        int64_t graph = 0;
        /// Pointer position in graph space when the paste command was invoked.
        godot::Vector2 anchor;
        /// Exact conflict rows as shown to the user, in the planner's order.
        godot::Array conflicts;
        /// One explicit or safe default choice per planned conflict, in planner order.
        godot::PackedStringArray choices;
        /// Candidate graph prepared by the confirmation validator.
        godot::Dictionary result;
        /// The inserted nodes to select after the accepted paste.
        godot::PackedInt64Array created;
        /// Native lifecycle callbacks that became callable events during the accepted merge.
        int64_t converted_lifecycle = 0;
        /// A resource edit arrived while the review was open.
        bool stale = false;
    };
    /// Drops the snapshots as soon as the review has finished.
    std::optional<PendingDocumentPaste> pending_document_paste;
    /// Store native Godot class names and inheritance for the searchable palette.
    std::map<godot::StringName, NativeClass> native_classes;
    /// Cache class enums on the first enum picker search.
    godot::Array native_enum_options;
    /// Reflected options for the current palette, reused while filtering search.
    godot::Array member_options;
    /// The reflected receiver's own options precede the ordinary catalogue.
    int64_t receiver_member_count = 0;
    /// Value methods, globals, and native static methods available without a specific receiver.
    godot::Array catalog_options;
    /// Snapshot registered singletons and project Autoloads for the current palette.
    godot::Array singleton_options;
    /// Runtime Object classes available as native cast destinations.
    godot::PackedStringArray native_cast_types;
    /// Snapshot registered project script types when opening the cast picker.
    godot::Array script_cast_types;
    /// Palette window owned by the graph window.
    godot::PopupPanel *palette = nullptr;
    /// Palette search field.
    godot::LineEdit *search = nullptr;
    /// Keep the pin compatibility filter as a preference for this editor window.
    godot::CheckBox *context_sensitive = nullptr;
    /// Expandable palette results.
    godot::Tree *results = nullptr;
    /// Enable insertion only for a selectable palette action.
    godot::Button *add_button = nullptr;
    /// Native menu shared by node, pin, and wire context clicks.
    godot::PopupMenu *context_menu = nullptr;
    /// Alignment choices in the node context menu.
    godot::PopupMenu *alignment_menu = nullptr;
    /// Graph-wide commands that do not create nodes.
    godot::PopupMenu *graph_tools_menu = nullptr;
    /// Root breadcrumb doubles as a return action only while editing a subgraph.
    godot::Button *event_root = nullptr;
    /// Separates the root and a definition graph in the breadcrumb.
    godot::TextureRect *graph_separator = nullptr;
    /// The current function or macro in the breadcrumb.
    godot::Label *graph_caption = nullptr;
    /// Anchored result panel for existing nodes and declaration references.
    godot::PanelContainer *navigation_search = nullptr;
    /// Editable search field in the graph toolbar.
    godot::LineEdit *navigation_query = nullptr;
    /// Existing nodes grouped by their owning graph.
    godot::Tree *navigation_results = nullptr;
    /// Filter search results to a particular declaration when set.
    SymbolAddress search_symbol;
    /// Original card colors keyed by instance identity while the current search dims them.
    std::map<uint64_t, godot::Color> navigation_dimmed_cards;
    /// Camera and selection states visited before the current location.
    std::vector<GraphLocation> back_locations;
    /// Camera and selection states after the current history position.
    std::vector<GraphLocation> forward_locations;
    /// Invalidate deferred camera placement after another navigation action.
    uint64_t camera_request = 0;
    /// Shows a save or reload failure that needs an explicit acknowledgement.
    editor_ui::EditorModal *problem_dialog = nullptr;
    /// Confirms saving or discarding the active graph before switching scripts.
    editor_ui::EditorModal *switch_dialog = nullptr;
    /// Confirms discarding in-memory edits before reloading the file.
    editor_ui::EditorModal *reload_dialog = nullptr;
    /// Confirm a variable change that resets default values, links, or Inspector overrides.
    editor_ui::EditorModal *variable_edit_dialog = nullptr;
    /// Review replacements or renamed copies before pasting a whole script.
    editor_ui::EditorModal *document_paste_dialog = nullptr;
    /// Owner, snapshots, and undo label for the pending variable change.
    godot::Ref<VisualScript> variable_edit_owner;
    godot::Dictionary variable_edit_before;
    godot::Dictionary variable_edit_next;
    godot::String variable_edit_title;
    godot::Callable variable_edit_finalize;
    /// Position at which the palette will insert the next graph node.
    godot::Vector2 insertion_position;
    /// Optional pin to connect to the new node.
    PendingPin pending_pin;
    /// Canvas position where the wire was released to open the current picker.
    godot::Vector2 palette_wire_point;
    /// Graph-owned choice menu for a drop that can create more than one node kind.
    godot::Control *drop_choice = nullptr;
    /// Connection captured when the user opens a menu over a wire.
    godot::Dictionary hovered_wire;
    /// Keep a wire selected for choosing a node to splice into it.
    bool inserting_on_wire = false;
    /// A persistent data output used for a batch of clicks on input ports.
    PendingPin multi_connect_source;
    /// Collect hovered targets before a Shift-drag commits its compatible batch.
    godot::Array batch_connection_targets;
    /// Track an active Shift-drag originating from a data pin.
    bool batch_connecting = false;
    /// Allow click-to-connect without changing the default pin drag gesture.
    bool click_multi_connect = false;
    /// Keep a normal drag available for edge panning and closest-socket drop.
    bool native_wire_drag = false;
    /// Remember the native source if a normal drag switches to Shift multi-connect.
    PendingPin native_wire_source;
    /// Distinguish an editor-owned mouse gesture from GraphEdit keyboard wiring.
    bool pointer_wire_drag = false;
    /// Pointer position when a wire press began, for separating a click from a drag.
    godot::Vector2 wire_press_point;
    /// Last pointer position used for continuous edge panning.
    godot::Vector2 wire_pan_point;
    /// Track real node movement separately from Q, layout, and other model edits.
    bool native_node_drag = false;
    /// Suppress automatic topology edits on release if focus interrupted this move.
    bool interrupted_node_drag = false;
    /// A new card press can recover when Godot omitted the next begin-move signal after focus loss.
    bool rearm_node_move = false;
    /// Skip delayed focus cleanup if the user has already started a different gesture.
    uint64_t node_move_generation = 0;
    /// The canvas snap setting to restore after moving a comment and its members together.
    std::optional<bool> comment_drag_snap;
    /// Selected members moved by their parent frame, with native independent motion suppressed.
    godot::PackedInt64Array comment_drag_members;
    /// Track pointer sweeps only during a native node drag that excludes comment frames.
    bool shake_tracking = false;
    /// Current left-button motion, available when Godot confirms a native node drag.
    godot::Vector2 shake_motion;
    /// Accumulate raw canvas pointer travel independently of event frequency, snapping, and zoom.
    godot::Vector2 shake_displacement;
    /// Direction established by the first sweep and reversed at each completed return sweep.
    godot::Vector2 shake_direction;
    /// Furthest pointer position along the current sweep direction.
    godot::Vector2 shake_extreme;
    /// Number of deliberate direction reversals within this shake.
    int32_t shake_turns = 0;
    /// Time of the first completed sweep in the current shake.
    uint64_t shake_window_start = 0;
    /// Retain a recognized shake until the native drag commits.
    bool shake_pending = false;
    /// Connection set shown during a move. The document retains the original wires until release.
    std::optional<godot::Array> node_wire_preview;
    /// Coalesce preview updates until GraphEdit has applied the latest pointer movement.
    bool node_wire_preview_queued = false;
    /// First endpoint captured for a two-pin wire exchange.
    PendingPin exchange_source;
    /// Saved edge identities highlighted in the current view.
    godot::Array highlighted_wires;
    /// Wires collected during a single Alt+middle-button sweep.
    godot::Array swept_wires;
    /// Last position of the sweep, in canvas pixels.
    godot::Vector2 sweep_point;
    /// Whether a sweep is active.
    bool sweeping_wires = false;
    /// When enabled, a moved node can splice into a hovered wire.
    bool automatic_insert = true;
    /// When enabled, connect compatible adjacent nodes after a move.
    bool automatic_connect = false;
    /// Show all data wires as neutral strokes while retaining typed sockets.
    bool neutral_wires = false;
    /// Recorded mouse modifier used to suppress automatic gestures.
    bool suppress_automatic = false;
    /// Exact canvas location used to insert a dot on the selected wire.
    godot::Vector2 hovered_wire_point;
    /// Suppresses redraw during a local mutation that preserves existing widgets.
    bool applying_edit = false;
    /// Whether a deferred canvas redraw is scheduled.
    bool rebuild_scheduled = false;
    /// Keep selection restoration from replacing an active side-panel selection.
    bool rebuilding_canvas = false;
    /// Native edges are absent while their port rows cannot be laid out.
    bool canvas_wires_suspended = false;
    /// Coalesce visibility and focus signals into one refresh after the window transition.
    bool canvas_wire_refresh_queued = false;

    /// Replace the active resource after any unsaved-change confirmation.
    void use_script(const godot::Ref<VisualScript> &value);
    /// Rebuild the visual canvas from the active resource.
    void rebuild();
    /// Schedule a redraw after a model change and refresh command states.
    void graph_changed();
    /// Apply a pending redraw after Godot releases the active node drag.
    void rebuild_when_idle();
    /// Check whether the canvas can supply native port positions for its visible rows.
    bool canvas_ports_available() const;
    /// Discard native edges immediately when hiding and republish once the layout resumes.
    void canvas_visibility_changed();
    /// Recheck window state after pending focus, visibility, and layout notifications.
    void schedule_canvas_wire_refresh();
    /// Reconcile native edges with the current graph after a window transition.
    void refresh_canvas_wires();
    /// Project the document's stable pin IDs to valid native ports.
    void attach_canvas_wires(const godot::Array &connections);
    /// Read the active graph from the document, including function or macro graphs.
    godot::Dictionary current_graph(const godot::Dictionary &document) const;
    /// Commit the active graph or refuse an incompatible type change before modifying the script.
    bool commit_graph(const godot::String &title, const godot::Dictionary &graph, bool redraw = true, bool merge = false);
    /// Switch to the chosen graph without dropping other edits to the document.
    void open_graph(int64_t id);
    /// Restore a camera and selection without recording a second history entry.
    void restore_location(const GraphLocation &location);
    /// Place a saved camera after the canvas updates its scroll bounds.
    void place_location_camera(const godot::Vector2 &center, float zoom, uint64_t request);
    /// Capture the current view before opening another graph or result.
    GraphLocation graph_location() const;
    /// Add the current view to Back and discard the previous Forward branch.
    void record_location();
    /// Clear transient navigation when changing scripts.
    void reset_navigation();
    /// Resolve the visible event, function, or macro graph name.
    static godot::String graph_title(const godot::Dictionary &document, int64_t graph_id);
    /// Build the toolbar field and its anchored results.
    void build_navigation_search(godot::Node *toolbar);
    /// Step backward or forward, skipping graph locations deleted in the meantime.
    void step_navigation(bool back);
    /// Handle navigation shortcuts outside graph text editing controls.
    bool navigation_shortcut(const godot::Ref<godot::InputEventKey> &key);
    /// Center and fit either the selection or all nodes.
    void frame_nodes(bool selection_only);
    /// Frame a directly opened function after its card layout settles if navigation has stayed there.
    void frame_opened_function(int64_t graph_id, uint64_t request);
    /// Open the existing-node search.
    void find_graph_nodes();
    /// Open the search UI in text or declaration reference mode.
    void show_navigation_search(const SymbolAddress &symbol);
    /// Rebuild matching existing nodes across the script.
    void refresh_navigation_search(const godot::String &query);
    /// Show the results beneath the toolbar while the field owns the current query.
    void update_navigation_query(const godot::String &query);
    /// Keep the result rectangle aligned with the search field across layout changes.
    void place_navigation_search();
    /// Restore the exact card colors that were present before filtering.
    void restore_navigation_card_colors();
    /// Dismiss the result panel and restore filtered styling while leaving focus with its current owner.
    void close_navigation_search();
    /// Dismiss on Escape or outside pointer input before native canvas gestures.
    bool dismiss_navigation_input(const godot::Ref<godot::InputEvent> &event);
    /// Move the highlighted result while keeping focus and the caret in the query.
    void navigation_search_input(const godot::Ref<godot::InputEvent> &event);
    /// Activate a mouse-selected result after the Tree finishes the event.
    void navigation_result_clicked(godot::Vector2 point, int64_t button);
    /// Activate the selected search result and reveal it in its graph.
    void reveal_navigation_result();
    /// Resolve the declaration of a node addressed in a graph.
    SymbolAddress declaration_address(const godot::Dictionary &node, int64_t owner) const;
    /// Resolve the declaration referenced by the only selected graph node.
    SymbolAddress selected_declaration_address() const;
    /// Whether the selected node refers to a user-authored declaration.
    bool has_selected_declaration() const;
    /// Reveal the referenced declaration, entering its graph if it has a body.
    void go_to_definition(int64_t node_id);
    /// Open the authored declaration when its canvas node is double clicked.
    void navigation_node_input(const godot::Ref<godot::InputEvent> &event, int64_t node_id);
    /// Dispatch a navigation command from a context or tools menu.
    bool navigation_palette_action(const godot::Dictionary &metadata);
    /// Save the current graph view to a local numbered quick jump.
    void save_quick_jump(int64_t slot);
    /// Restore a numbered quick jump in the current script.
    void recall_quick_jump(int64_t slot);
    /// Refresh details after Godot changes the canvas selection.
    void canvas_selection_changed(godot::Node *node);
    /// Find the open graph prompt before dispatching background input.
    editor_ui::EditorModal *active_modal() const;
    /// End graph gestures and close any command popup before a prompt takes input.
    void prepare_modal();
    /// Show a problem that needs an acknowledgment in the workbench.
    void show_problem(const godot::String &message);
    /// Refresh the title and current graph commands.
    void refresh_commands();
    /// Commit one graph change, optionally merging successive edits to a text field.
    void commit(const godot::String &title, const godot::Dictionary &next, bool redraw = true, bool merge = false,
            const godot::Callable &redo_view = {}, const godot::Callable &undo_view = {});
    /// Save the graph and switch to the pending resource if saving succeeds.
    void save_and_switch();
    /// Restore the last saved graph and switch to the pending resource.
    void discard_and_switch();
    /// Keep the current resource and release the pending resource.
    void cancel_switch();
    /// Save the active script and report whether its graph is persisted.
    bool save_graph(bool interactive = true);
    /// Schedule a write after a completed editor transaction.
    void schedule_autosave();
    /// Disconnect the pending write before reloading, switching, or saving.
    void cancel_autosave();
    /// Persist the completed edit after the idle interval.
    void autosave_graph();
    /// Ask before discarding a draft to reload the saved file.
    void request_reload();
    /// Reopen the script from disk when it changed outside the editor.
    void reload_graph();
    /// Apply the previous graph revision.
    void undo_graph();
    /// Apply the next graph revision.
    void redo_graph();
    /// Run window save and graph history shortcuts.
    void shortcut(const godot::Ref<godot::InputEvent> &event);
    /// Keep graph input out of the UI layer currently covering the canvas.
    bool canvas_input_blocked() const;
    /// Collect the runtime-available native Node hierarchy from ClassDB.
    void collect_native_classes();
    /// Open the palette near a canvas point.
    void show_palette(godot::Vector2 point);
    /// Position a native Godot popup relative to this workspace inside the editor viewport.
    void popup_in_workspace(godot::Window *popup, godot::Vector2 point, godot::Vector2i size);
    /// Open with the optional pending pin retained.
    void open_palette(godot::Vector2 point);
    /// Clear the picker gesture after choosing an entry or canceling.
    void palette_closed();
    /// Resolve the retained connector from its live socket to the picker border.
    bool palette_wire_geometry(godot::Vector2 &from, godot::Vector2 &to, godot::Color &color) const;
    /// Open the palette near the center for keyboard and toolbar users.
    void show_center_palette();
    /// Update placement and offer the actions carried by a dropped scene or script symbol.
    void show_dropped_options(godot::Vector2 point, const godot::Array &options);
    /// Open a choice after Godot has finished the drag and the source window's mouse release.
    void open_dropped_options(godot::Vector2 origin, godot::Vector2 position, const godot::Array &options,
            const godot::Ref<VisualScript> &owner, int64_t graph);
    /// Insert the chosen node at its original drop position if its graph is still open.
    void accept_drop_choice(const godot::Dictionary &node, godot::Vector2 position,
            const godot::Ref<VisualScript> &owner, int64_t graph);
    /// Dismiss the drop choices and return keyboard focus to the graph canvas.
    void close_drop_choice();
    /// Place a typed reference or offer PackedScene instantiation from a FileSystem drop.
    void drop_asset(godot::Vector2 point, const godot::String &path);
    /// Verify that the pending change still applies to the graph shown in the modal.
    bool validate_variable_edit();
    /// Commit the pending declaration and any wire changes in one undo step.
    void confirm_variable_edit();
    /// Release the captured edit when the user cancels or completes it.
    void clear_variable_edit();
    /// Resolve the getter or setter and its target using the same rules for preview and committed drops.
    godot::Dictionary variable_drop_node(const godot::Array &options, godot::Vector2 point, PendingPin &target) const;
    /// Paint the wire compatibility badge when a declaration can connect to its hovered pin.
    void preview_variable_drop(godot::Vector2 point, const godot::Array &options);
    /// Open a context-sensitive palette after dropping an output into empty space.
    void show_output_palette(godot::StringName node, int64_t port, godot::Vector2 point);
    /// Open a context-sensitive palette after dropping an input into empty space.
    void show_input_palette(godot::StringName node, int64_t port, godot::Vector2 point);
    /// Rebuild grouped palette results for the current query.
    void filter_palette(const godot::String &text);
    void refresh_palette_members();
    bool palette_filters_pin() const;
    /// Size the command palette for this invocation within the graph window.
    godot::Vector2i palette_extent();
    /// List methods, properties, and signals gathered for this palette invocation.
    void add_members(godot::TreeItem *root, const godot::Dictionary &document, const godot::Dictionary &graph,
            const godot::StringName &base, const godot::String &query, godot::TreeItem *&first);
    /// Add a native class and its matching descendants while retaining their hierarchy.
    godot::TreeItem *add_native_class(godot::TreeItem *parent, const godot::StringName &name,
            const std::map<godot::StringName, bool> &matches, const godot::String &query, godot::TreeItem *&first);
    /// Update whether the selected result is actionable.
    void palette_selection_changed();
    void palette_row_clicked(godot::Vector2 point, int64_t button);
    /// Move keyboard focus from search to the native result tree.
    void palette_search_input(const godot::Ref<godot::InputEvent> &event);
    /// Enter a selected search result without requiring a mouse.
    void submit_search(const godot::String &text);
    /// Add the selected palette entry and any pending connection.
    void insert_selected();
    void insert_palette_result(const godot::Dictionary &metadata);
    /// Build the menus outside the creation picker.
    void build_graph_menus(godot::Node *toolbar);
    /// Refresh graph-wide commands and current wiring preferences.
    void refresh_graph_tools();
    /// Open a node, pin, or wire menu when its visible canvas item is clicked.
    bool open_graph_context(godot::Vector2 point);
    /// Populate actions that apply to the active node selection.
    void add_node_context_actions();
    /// Populate actions that apply to the clicked socket.
    void add_pin_context_actions();
    /// Offer a receiver control only on a compatible property getter without a Target wire.
    void add_target_visibility_action(int64_t node_id, const godot::Dictionary &node, const godot::Array &connections);
    /// Record receiver visibility as an ordinary undoable graph edit.
    void set_target_visible(int64_t node_id, bool visible);
    /// Run the menu command after restoring canvas focus.
    void run_context_action(int64_t id, godot::PopupMenu *menu);
    /// Change compile state for eligible selected nodes in one undo action.
    void set_selection_disabled(bool disabled);
    /// Whether this graph node can pause execution before performing its action.
    bool can_break_at_node(const godot::Dictionary &document, const godot::Dictionary &graph,
            const godot::Dictionary &node) const;
    /// Toggle the breakpoint across eligible selected nodes, or apply an explicit state.
    void set_selection_breakpoints(bool enabled);
    /// Whether a breakpoint has been set for this node in the active graph.
    bool debug_has_breakpoint(int64_t node) const;
    /// Change editor-only breakpoint state for this node in the active graph.
    void debug_set_breakpoint(int64_t node, bool enabled);
    /// Whether the debugger is currently paused before executing this node.
    bool debug_is_current(int64_t node) const;
    /// Update breakpoint and paused markers without rebuilding or moving nodes.
    void refresh_debug_markers();
    /// Insert a panel or palette action as one edit. A pin drop requires a successful wire.
    void insert_node(const godot::Dictionary &record, bool require_connection = false);

    /// Browse for a project GDScript and build a typed constructor at the picker position.
    void choose_script_constructor();

    /// Insert the chosen constructor if the originating graph is still open.
    void insert_script_constructor(const godot::String &path, const godot::Ref<VisualScript> &owner, int64_t graph, godot::Vector2 point);
    /// Declare a custom event, place its entry, and open its declaration in Details.
    void insert_custom_event();
    /// Declare a value and connect its getter or setter as one authoring action.
    void promote_pin(bool local);
    /// Collect stable IDs for the canvas's currently selected nodes.
    godot::PackedInt64Array selected_nodes() const;
    /// Change positions in one undo action using actual node bounds.
    void layout_selection(LayoutCommand command);
    /// Match connected socket heights across the selection, or on a single hovered wire.
    void straighten_connections(const godot::Dictionary &wire = {});
    /// Extract the selected nodes into a function or macro and preserve its boundary wires.
    void collapse_selection(bool macro);
    /// Insert a same-type dot on the nearest wire under the cursor.
    void reroute_wire(godot::Vector2 point);
    /// Change the Print node's literal argument.
    void set_message(const godot::String &text, int64_t node_id);
    /// Render the declared data pins of a native property or method node.
    void draw_member(godot::GraphNode *widget, const godot::StringName &kind, const godot::Dictionary &node);
    /// Add the typed value editor beside an unwired input pin's label.
    void add_literal(godot::HBoxContainer *row, int64_t id, const godot::Dictionary &node,
            const godot::Dictionary &pin, const godot::Dictionary &document, bool preload = false);
    /// Add an input or remove the entry identified by a selected input pin in one graph edit.
    void edit_collection_constructor(int64_t id, const godot::StringName &remove_pin);
    /// Append a Sequence output or remove it with its wires, preserving surviving branch order.
    void edit_sequence_pins(int64_t id, const godot::StringName &remove_pin);
    /// Persist a typed literal on a reflected pin and report whether the graph accepted it.
    bool set_literal(const godot::Variant &value, int64_t id, const godot::StringName &pin, bool merge = true);
    /// Refresh visual pin types and slot contracts after a literal change without replacing focused controls.
    void refresh_pin_presentations();
    /// Persist a checkbox value on a Boolean pin.
    void set_bool_literal(bool value, int64_t id, const godot::StringName &pin);
    /// Store the underlying integer selected by name on an authored enum input.
    void set_enum_literal(int64_t index, const godot::Array &values, int64_t id, const godot::StringName &pin);
    /// Parse or cast the value entered in a text-backed literal.
    void set_text_literal(const godot::String &text, int64_t id, const godot::Dictionary &pin, int32_t component,
            godot::LineEdit *field);
    /// Commit a compatible saved project resource, reporting whether the field should display it.
    bool set_resource_literal(const godot::Ref<godot::Resource> &resource, int64_t id, const godot::Dictionary &pin);
    /// Deliver a native Quick Open choice only to the field that requested it.
    /// Persist all node positions changed by one drag.
    void finish_move();
    /// Create a validated execution or data connection.
    void connect_nodes(godot::StringName from, int64_t from_port, godot::StringName to, int64_t to_port);
    /// Remove a single wire from the saved model.
    void disconnect_nodes(godot::StringName from, int64_t from_port, godot::StringName to, int64_t to_port);
    /// Delete graph nodes and their attached wires in one undo action.
    void delete_selected(const godot::TypedArray<godot::StringName> &names);
    /// Copy the selected nodes and their internal connections.
    void copy_selected_nodes();
    /// Copy and remove the selected nodes and their internal connections.
    void cut_selected_nodes();
    /// Paste a compatible graph clipboard at the pointer.
    void paste_nodes();
    /// Decode and plan a whole-script paste into the current Event Graph.
    void paste_document(const godot::String &encoded, godot::Vector2 anchor);
    /// Display the available resolutions and keep the destination unchanged while they are reviewed.
    void review_document_paste(PendingDocumentPaste paste);
    /// Update one choice and enable Paste once every conflict has a resolution.
    void choose_document_paste_conflict(bool selected, int64_t index, const godot::String &choice);
    /// Reject stale choices and stage a complete, validated graph before the review closes.
    bool validate_document_paste();
    /// Commit the staged paste now that the modal has released background input.
    void confirm_document_paste();
    /// Release a canceled or completed review and its controls.
    void clear_document_paste();
    /// Record the entire paste as one undo action and reveal the inserted nodes.
    void finish_document_paste(const PendingDocumentPaste &paste);
    /// Clone selection without altering the clipboard, optionally keeping incoming data wires.
    void duplicate_selected_nodes(bool keep_inputs);
    /// Dispatch graph-edit shortcuts that are scoped to the canvas.
    bool graph_tool_shortcut(const godot::Ref<godot::InputEventKey> &key);
    /// Dispatch canvas-specific rendering and connector commands.
    bool run_canvas_wire_tool(const godot::String &command);
    /// Run an edit or selection command from a graph menu or shortcut.
    void run_graph_tool(const godot::Dictionary &command);
    /// Resolve a native canvas edge to the saved graph endpoints.
    godot::Dictionary saved_wire(const godot::Dictionary &wire) const;
    /// Locate a nearby socket; optional direction narrows compatible candidates.
    PendingPin pin_near(godot::Vector2 point, int direction = 0) const;
    /// Handle editor wire tools, returning true when the native canvas should ignore the event.
    bool wire_tool_input(const godot::Ref<godot::InputEvent> &event);
    /// Reserve pin modifiers before GraphEdit's top connection layer starts a native drag.
    bool early_wire_tool_input(const godot::Ref<godot::InputEvent> &event);
    /// Capture the native wire drag lifecycle for panning.
    void native_wire_started(const godot::StringName &node, int64_t port, bool output);
    /// Release state tied to the native wire drag when it ends.
    void native_wire_ended();
    /// Stop a native drag before the matching widgets or their sockets can be replaced.
    void cancel_native_wire();
    /// Drop pointer gestures on loss of focus without committing pending connections.
    void cancel_wire_gestures();
    /// Resolve the opposite socket and refresh the pointer preview.
    PendingPin preview_pointer_wire(godot::Vector2 point);
    /// Translate motion and release into the existing wire and picker commands.
    bool track_pointer_wire(const godot::Ref<godot::InputEvent> &event);
    /// Pan while an active wire is held at the canvas edge.
    void tick_wire_pan(double delta);
    /// Record the start of a native node move for optional wire gestures.
    void native_move_started();
    /// Recover the native move lifecycle when the pressed card moves after an interrupted drag.
    void recover_node_move_start(godot::GraphNode *card);
    /// Complete an interrupted native movement if Godot did not dispatch its move-finished signal.
    void finish_interrupted_move(uint64_t generation);
    /// Reset the sweep tracking for a new gesture or a timeout.
    void clear_shake_strokes();
    /// Recognize two deliberate returns against the drag's initial sweep.
    void track_shake_motion(godot::Vector2 relative);
    /// Prefer a nearby compatible socket when dropping a wire on a node body.
    bool try_lazy_connection(const PendingPin &source, godot::Vector2 point);
    /// Start the compatible-node picker for the wire context.
    void open_wire_insert();
    /// Add a newly authored node into the selected wire as one transaction.
    void insert_node_on_wire(const godot::Dictionary &record);
    /// Preview a recognized shake or valid automatic wiring without touching the document.
    godot::Dictionary node_move_wiring(const godot::Dictionary &moved_graph, godot::String &title) const;
    /// Resolve a saved pin to its canvas-local socket position.
    bool canvas_pin_position(int64_t node_id, const godot::StringName &pin, bool output, godot::Vector2 &position) const;
    /// Schedule a preview after the native canvas applies its pending movement.
    void queue_node_wire_preview();
    /// Compute and show the current candidate topology without editing the script.
    void refresh_node_wire_preview();
    /// Show the supplied connections or restore the saved graph when the candidate clears.
    void apply_node_wire_preview(const godot::Array &saved, const std::optional<godot::Array> &preview);
    /// Refresh the temporary wire and the tentative automatic edit during pointer movement.
    void preview_wire_motion(godot::Vector2 point);
    /// Refresh persisted-by-ID wire highlights after canvas rebuild.
    void update_wire_highlights();
    /// Clear graph-specific transient wiring tools and their selection.
    void reset_wire_tools();
    /// Rebuild native comment frames and apply their saved membership.
    void rebuild_graph_comments(const godot::Dictionary &graph);
    /// Use a common translation for a comment frame and its attached nodes.
    void begin_comment_drag();
    /// Match Godot's temporary snap toggle so the whole comment group moves without rounding each member.
    void update_comment_drag();
    /// Restore the canvas snap preference on release or cancellation.
    void end_comment_drag();
    /// Persist the position and size of the native comment frames after a gesture.
    godot::Dictionary capture_graph_comments(const godot::Dictionary &graph) const;
    /// Store the current geometry after resizing a comment frame.
    void finish_comment_resize(const godot::Vector2 &size);
    /// Rename the selected frame without rebuilding the current canvas.
    void rename_graph_comment(const godot::String &title, int64_t id, godot::LineEdit *input);
    /// Link dropped graph nodes to a comment through Godot's native frame gesture.
    void attach_comment_members(const godot::TypedArray<godot::StringName> &names, const godot::StringName &frame);

protected:
    /// Keep the workspace private to the editor plugin.
    static void _bind_methods() {}

public:
    /// Release the graph's history object before freeing the workspace.
    ~VisualScriptWindow() override;
    /// Build the workspace and bind the main-screen activation request.
    void build(const godot::Callable &reveal);
    /// Open or focus a script resource on the Visual Script main screen.
    void open(const godot::Ref<VisualScript> &value);
    /// Resume a Custom Script draft after the workspace becomes active again.
    void enter_workspace();
    /// Dismiss transient layers when leaving the main screen and retain the current graph draft.
    void leave_workspace();
    /// Handle shortcuts only while the workspace and its controls own keyboard input.
    void _input(const godot::Ref<godot::InputEvent> &event) override;
    /// Save the active graph during the editor's Save All flow.
    void save_external_data();
    /// Flush the graph before Godot launches a game.
    bool save_before_run();
    /// Provide the standard editor quit warning for an unsaved graph.
    godot::String unsaved_status() const;
};

/// Register the native canvas with typed drag validation.
void register_visual_script_canvas();

}

#endif
