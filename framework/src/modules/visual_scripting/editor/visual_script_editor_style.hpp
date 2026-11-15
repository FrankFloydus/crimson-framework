// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"

#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace crimson::visual_scripting {

/// Graph workspace colors for the framed shell, native controls, and focus states.
enum class GraphTone {
    SHELL, SIDEBAR, PANE, CANVAS, HEADER, GROUP, FIELD, DIVIDER, HOVER, SELECTED, TEXT, MUTED, HINT
};

/// Resolve graph workspace roles to the editor component palette.
inline godot::Color graph_color(GraphTone tone) {
    using editor_ui::Surface;
    constexpr Surface palette[] = {Surface::CANVAS, Surface::SIDEBAR, Surface::HEADER, Surface::CANVAS, Surface::HEADER,
        Surface::RAISED, Surface::CANVAS, Surface::RULE, Surface::HOVER, Surface::SELECTED,
        Surface::TEXT, Surface::MUTED, Surface::MUTED};
    return editor_ui::color(palette[static_cast<unsigned>(tone)]);
}

/// Keep native menu icons aligned with the tool's muted utility controls.
inline void set_graph_menu_icon(godot::PopupMenu *menu, int32_t index, const godot::StringName &name) {
    if (name.is_empty()) {
        return;
    }
    const bool shared = menu->has_theme_icon(name, "CrimsonIcons");
    const godot::StringName family = shared ? "CrimsonIcons" : "EditorIcons";
    if (!menu->has_theme_icon(name, family)) {
        return;
    }
    menu->set_item_icon(index, menu->get_theme_icon(name, family));
    menu->set_item_icon_max_width(index, editor_ui::pixels(menu, 16));
    if (!shared) {
        menu->set_item_icon_modulate(index, graph_color(GraphTone::MUTED));
    }
}

} // namespace crimson::visual_scripting

#endif
