// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "editor_controls.hpp"

#ifdef DEBUG_ENABLED

#include <map>

namespace godot {
class HSplitContainer;
class MarginContainer;
class PanelContainer;
class SplitContainer;
class VSplitContainer;
}

namespace crimson::editor_ui {

/// A reusable selectable hierarchy with caller-supplied row content and actions.
class EditorTree final : public godot::VBoxContainer {
    GDCLASS(EditorTree, godot::VBoxContainer)

    /// Stock controls owned by this tree and parent information for keyboard navigation.
    struct Row {
        /// The enclosing parent ID, or empty for a root item.
        godot::StringName parent;
        /// The nested container for a row and all descendants.
        godot::VBoxContainer *branch = nullptr;
        /// Keyboard-focusable row used for background, selection and ordinary navigation.
        godot::Button *target = nullptr;
        /// Full-width row containing optional interactive content and actions.
        godot::HBoxContainer *line = nullptr;
        /// Expansion trigger shown only for items with descendants.
        godot::Button *expand = nullptr;
        /// Indentation placeholder used until an item gains children.
        godot::Control *spacer = nullptr;
        /// Left branch guide behind the nested children.
        godot::PanelContainer *guide = nullptr;
        /// Descendant branch container, hidden while collapsed.
        godot::VBoxContainer *children = nullptr;
        /// Stable ordered child IDs.
        std::vector<godot::StringName> order;
        /// The current branch disclosure.
        bool expanded = true;
    };

    /// Index by stable application item ID.
    std::map<godot::StringName, Row> rows;
    /// Stable order of top-level items.
    std::vector<godot::StringName> roots;
    /// Current selection. A programmatic selection does not emit a callback.
    godot::StringName selected;
    /// Receives the selected item ID after user activation.
    godot::Callable on_selection;

    /// Notify the caller after a user selects a row.
    void activate(godot::StringName id);
    /// Toggle the clicked branch disclosure without changing selection.
    void toggle(godot::StringName id);
    /// Handle sibling and parent/child navigation for a focused row.
    void navigate(const godot::Ref<godot::InputEvent> &event, godot::StringName id);
    /// Add visible item IDs in display order.
    void visible_rows(const std::vector<godot::StringName> &ids, std::vector<godot::StringName> &visible) const;

protected:
    /// This internal component exposes no script bindings.
    static void _bind_methods() {}

public:
    /// Store the callback that receives a user-selected item ID.
    void configure(const godot::Callable &selection_changed);
    /// Add a row and adopt its detached body and actions. An optional background distinguishes groups at rest.
    /// On duplicate IDs or a missing parent, ownership remains with the caller.
    void add_row(const godot::StringName &id, godot::Control *body, const godot::StringName &parent = {},
            godot::Control *actions = nullptr, bool expanded = true, const godot::String &help = {},
            godot::Color background = godot::Color(0, 0, 0, 0));
    /// Remove every row and reset the selection.
    void clear();
    /// Set selection without notification and optionally focus that row.
    void set_selected(const godot::StringName &id, bool focus = false);
    /// Return the currently selected application ID.
    godot::StringName get_selected() const;
    /// Set the disclosure of an existing branch.
    void set_expanded(const godot::StringName &id, bool expanded);
    /// Read the current disclosure of an existing branch.
    bool is_expanded(const godot::StringName &id) const;
};

/// Short C++ spelling for the tree factory and its tool consumers.
using Tree = EditorTree;

/// Attach an empty tree. Selection changed receives a stable application item ID.
Tree *tree(godot::Node *owner, const godot::Callable &selection_changed = {});

/// Attach a flat work surface, leaving dividers and content spacing to the owner.
godot::PanelContainer *surface_panel(godot::Node *owner, Surface role);

/// Add symmetric logical padding for any workbench content slot.
godot::MarginContainer *inset(godot::Node *owner, int horizontal, int vertical);

/// Attach an inset card that owns optional header, body, and footer slots.
godot::PanelContainer *card(godot::Node *owner, godot::Control *header, godot::Control *body,
        godot::Control *footer = nullptr, godot::Color background = color(Surface::CANVAS));

/// Attach a label and value on one inspector line with equivalent pointer and keyboard help.
godot::HBoxContainer *field(godot::Node *owner, const godot::String &name, godot::Control *value,
        const godot::String &help = {}, int label_width = 80);

/// Attach labeled fields below an optional section heading.
godot::VBoxContainer *field_group(godot::Node *owner, const godot::String &heading = {});

/// Attach a compact status with a next action and optional visual.
godot::PanelContainer *empty_state(godot::Node *owner, const godot::String &title, const godot::String &description,
        godot::Control *action = nullptr, godot::Control *media = nullptr);

/// Attach a row with optional media, descriptive text, and a trailing action.
godot::HBoxContainer *item_row(godot::Node *owner, const godot::String &title, const godot::String &description,
        godot::Control *media = nullptr, godot::Control *action = nullptr);

/// Attach a horizontal split with optional detached panes and native pointer and keyboard resize.
godot::HSplitContainer *split_pane(godot::Node *owner, godot::Control *left, godot::Control *right);

/// Attach a vertical split with optional detached panes and native pointer and keyboard resize.
godot::VSplitContainer *split_pane_vertical(godot::Node *owner, godot::Control *top, godot::Control *bottom);

/// Register the internal generic Tree at editor initialization.
void register_editor_compositions();

}

#endif
