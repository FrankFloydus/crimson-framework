// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "editor_compositions.hpp"

#ifdef DEBUG_ENABLED

#include "editor_graph.hpp"
#include "editor_spacing.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/button_group.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/check_button.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/graph_edit.hpp>
#include <godot_cpp/classes/h_split_container.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/menu_button.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/tab_container.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/v_split_container.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>

namespace crimson::editor_ui {
using namespace godot;

namespace {

/// Attach one stock widget to a catalog surface.
template <typename T>
T *add(Node *owner) {
    T *control = memnew(T);
    if (owner) {
        owner->add_child(control);
    }
    return control;
}

/// The catalog uses this sequence for sidebar display and page construction.
enum Component {
    BUTTON, TEXT_INPUT, TEXT_AREA, NUMERIC_INPUT, SLIDER, CHECKBOX, SWITCH, RADIO, SELECT, COMBOBOX,
    DISCLOSURE, TABS, MENU, TOOLTIP, PROGRESS, TREE, CARD, FIELD_GROUP, EMPTY_STATE, ITEM_ROW, SPLIT_PANE, GRAPH_NODE, DIALOG,
    COMPONENT_COUNT
};

/// User-facing catalog names in Component order.
constexpr const char *component_names[COMPONENT_COUNT] = {
    "Button", "Text Input", "Text Area", "Numeric Input", "Slider", "Checkbox", "Switch", "Radio", "Select", "Combobox",
    "Disclosure", "Tabs", "Menu", "Tooltip", "Progress", "Tree", "Card", "Field Group", "Empty State", "Item Row", "Split Pane",
    "Graph Node", "Dialog"
};

/// Short purpose text for each page in Component order.
constexpr const char *component_descriptions[COMPONENT_COUNT] = {
    "Run a command. Icon-only buttons expose a label and tooltip.",
    "Enter or search one line of text. Read-only fields keep keyboard selection and copy.",
    "Edit a multiline description or note.",
    "Enter a precise value or use the adjacent minus and plus buttons.",
    "Adjust a bounded value by dragging or pressing the arrow keys.",
    "Toggle an independent choice. Mixed represents values that differ across the current selection.",
    "Turn a setting on or off immediately.",
    "Choose one option from a short visible list.",
    "Choose from a known list. Each option can render caller-supplied content, including a preview icon, title, and description.",
    "Filter a list by typing, then choose one of its items.",
    "Expand or collapse a short section.",
    "Switch between peer panels in the same workspace.",
    "Group commands behind one trigger. Escape closes the menu and restores focus.",
    "Show brief help for a control.",
    "Show completion when the amount of work is known, or activity when it is unknown.",
    "A selectable hierarchy with collapsible branches and an optional action slot on each row.",
    "A container with optional header, content, footer, and background.",
    "Group related fields under a heading.",
    "A compact message with optional media and a next action.",
    "Show a resource or result with media, text and an optional action.",
    "Drag the divider or focus it and use the arrow keys to resize the panels.",
    "A draggable card with a compact title and typed sockets. Connected sockets are filled.",
    "Ask for a choice or show a short form inside the workbench."
};

/// Apply symmetric padding to a sample region.
void padding(MarginContainer *control, int horizontal, int vertical) {
    control->add_theme_constant_override("margin_left", pixels(control, horizontal));
    control->add_theme_constant_override("margin_right", pixels(control, horizontal));
    control->add_theme_constant_override("margin_top", pixels(control, vertical));
    control->add_theme_constant_override("margin_bottom", pixels(control, vertical));
}

/// Resolve an authored spacing token against the C++ layout constants.
int style_spacing(const String &token) {
    const struct { const char *name; int pixels; } values[] = {
        {"xxs", spacing::xxs}, {"xs", spacing::xs}, {"sm", spacing::sm}, {"md", spacing::md},
        {"lg", spacing::lg}, {"xl", spacing::xl}, {"xxl", spacing::xxl}
    };
    for (const auto &entry : values) {
        if (token == entry.name) {
            return entry.pixels;
        }
    }
    return 0;
}

/// Add guide copy that wraps to fit the spare width at the catalog minimum size.
void guide_copy(Node *owner, const String &text, LabelStyle tone = LabelStyle::BODY) {
    if (!text.is_empty()) {
        label(owner, text, tone)->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    }
}

/// Show native role cards and their typed socket rows within the guide.
void graph_node_example(Node *owner);

/// Reflow guide rows together when the viewport becomes too narrow for two columns.
void fit_guide_rows(ScrollContainer *viewport, const Array &responsive_rows) {
    const bool narrow = viewport->get_size().x < pixels(viewport, 620);
    for (const Variant &entry : responsive_rows) {
        Object *object = entry;
        Object::cast_to<BoxContainer>(object)->set_vertical(narrow);
    }
}

/// Paint the token itself at its actual size, keeping its demonstration column aligned.
void guide_spacing_mark(Node *owner, int logical) {
    auto *sample = add<HBoxContainer>(owner);
    sample->set_custom_minimum_size(Vector2(pixels(sample, 36), 0));
    sample->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    auto *bar = add<PanelContainer>(sample);
    bar->set_custom_minimum_size(Vector2(pixels(bar, logical), pixels(bar, spacing::xs)));
    bar->add_theme_stylebox_override("panel", box(color(Surface::HIGHLIGHT), Color(0, 0, 0, 0)));
    bar->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
}

/// Show the smallest shared action group and its two native button roles.
void guide_dialog_actions(Node *owner) {
    auto *row = add<HBoxContainer>(owner);
    row->set_alignment(BoxContainer::ALIGNMENT_END);
    row->add_theme_constant_override("separation", pixels(row, spacing::md));
    auto *cancel = button(row, "Cancel");
    cancel->set_theme_type_variation("CrimsonDialogSecondary");
    auto *confirm = button(row, "Remove");
    confirm->set_theme_type_variation("CrimsonDialogDanger");
    confirm->set_custom_minimum_size(Vector2(pixels(row, 64), pixels(row, 32)));
}

/// Build guide examples with the same controls and theme items the authoring windows use.
void guide_specimen(Node *owner, const String &specimen) {
    if (specimen == "type_body" || specimen == "type_secondary" || specimen == "type_title") {
        guide_copy(owner, specimen == "type_body" ? "Choose a target" : specimen == "type_secondary" ? "Changes apply on confirmation" : "Input Map",
                specimen == "type_body" ? LabelStyle::BODY : specimen == "type_secondary" ? LabelStyle::MUTED : LabelStyle::TITLE);
    } else if (specimen == "content_tabs" || specimen == "keyboard_state") {
        auto *navigation = tabs(owner);
        auto *keyboard = label(navigation, "Arrow-key navigation and input bindings");
        keyboard->set_name("Keyboard");
        keyboard->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        auto *gamepad = label(navigation, "Controller buttons and axes");
        gamepad->set_name("Gamepad");
        gamepad->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    } else if (specimen == "radius_control" || specimen == "field_anatomy") {
        field(owner, "Action", text_input(nullptr, "Jump", "The action displayed in the binding tree."));
    } else if (specimen == "radius_popup") {
        auto *preview = add<PanelContainer>(owner);
        preview->add_theme_stylebox_override("panel", preview->get_theme_stylebox("panel", "PopupPanel"));
        auto *layout = add<VBoxContainer>(preview);
        auto *option = button(layout, "Rename", {}, {}, ButtonStyle::ROW);
        option->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        button(layout, "Remove", {}, {}, ButtonStyle::ROW)->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    } else if (specimen == "dialog_anatomy") {
        auto *preview = add<PanelContainer>(owner);
        preview->set_theme_type_variation("CrimsonDialogDestructivePanel");
        auto *layout = add<VBoxContainer>(preview);
        layout->add_theme_constant_override("separation", pixels(layout, spacing::md));
        label(layout, "Remove binding?")->set_theme_type_variation("CrimsonDialogTitle");
        auto *description = label(layout, "The input action stays in the map.");
        description->set_theme_type_variation("CrimsonDialogDescription");
        description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        guide_dialog_actions(layout);
    } else if (specimen == "graph_pins" || specimen == "graph_anatomy") {
        graph_node_example(owner);
    } else if (specimen == "field_states") {
        field(owner, "Read-only", text_input(nullptr, "res://input/jump.tres", "Copy the path from the keyboard.", FieldState::READ_ONLY));
        field(owner, "Disabled", text_input(nullptr, "Default", "This map setting is fixed.", FieldState::DISABLED));
    } else if (specimen == "tree_inspector" || specimen == "tool_workbench") {
        auto *bindings = surface_panel(nullptr, Surface::SIDEBAR);
        auto *inspector = surface_panel(nullptr, Surface::HEADER);
        auto *split = split_pane(owner, bindings, inspector);
        split->set_custom_minimum_size(Vector2(0, pixels(split, 94)));
        bindings->set_custom_minimum_size(Vector2(pixels(split, 100), 0));
        auto *binding_tree = tree(inset(bindings, spacing::xs, spacing::xs));
        binding_tree->add_row("locomotion", label(nullptr, "Move", LabelStyle::HEADING));
        binding_tree->add_row("keyboard", label(nullptr, "W"), "locomotion");
        binding_tree->set_selected("keyboard");
        auto *settings = field_group(inset(inspector, spacing::md, spacing::xs), "Inspector");
        field(settings, "Scale", text_input(nullptr, "1.0"), {}, 42);
    } else if (specimen == "menu_pattern") {
        auto *preview = menu(owner, "Action options");
        preview->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
        preview->get_popup()->add_item("Rename");
        preview->get_popup()->add_separator();
        preview->get_popup()->add_item("Remove");
    } else if (specimen == "selected_state" || specimen == "select_anatomy") {
        PackedStringArray options;
        options.append("Button");
        options.append("1D Axis");
        options.append("2D Vector");
        select(owner, options, 1);
    } else if (specimen == "dialog_actions") {
        guide_dialog_actions(owner);
    } else if (specimen == "atomic_controls") {
        checkbox(owner, "Enabled", CheckState::CHECKED);
        toggle_switch(owner, "Loop", false);
    } else if (specimen == "hover_state") {
        auto *row = add<HBoxContainer>(owner);
        row->add_theme_constant_override("separation", pixels(row, spacing::md));
        button(row, "Resting", {}, {}, ButtonStyle::QUIET);
        auto *hover = button(row, "Hovered", {}, {}, ButtonStyle::QUIET);
        hover->add_theme_stylebox_override("normal", hover->get_theme_stylebox("hover"));
    } else if (specimen == "pill_actions" || specimen == "button_anatomy") {
        auto *row = add<HBoxContainer>(owner);
        row->add_theme_constant_override("separation", pixels(row, spacing::md));
        button(row, "Save", "Save the current draft", {}, ButtonStyle::PRIMARY);
        icon_button(row, "search", "Find actions");
    }
}

/// Build the native Design Style page from the same authored section data as the gallery.
void build_design_style(TabContainer *navigation) {
    auto *page = add<ScrollContainer>(navigation);
    page->set_name("Design Style");
    page->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    page->set_follow_focus(true);
    auto *inset = add<MarginContainer>(page);
    inset->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    padding(inset, spacing::xxl, spacing::xl);
    auto *guide = add<VBoxContainer>(inset);
    guide->set_name("DesignStyleGuide");
    guide->add_theme_constant_override("separation", pixels(guide, spacing::xl));

    Ref<JSON> parser;
    parser.instantiate();
    const String path = "res://addons/crimson-framework/editor/design_style.json";
    if (!FileAccess::file_exists(path) || parser->parse(FileAccess::get_file_as_string(path)) != OK ||
            parser->get_data().get_type() != Variant::DICTIONARY) {
        guide_copy(guide, "Design guidance is unavailable. Check that design_style.json is present in the addon.", LabelStyle::MUTED);
        return;
    }
    const Dictionary document = parser->get_data();
    guide_copy(guide, document.get("intro", String()));
    Array responsive_rows;
    const Array sections = document.get("sections", Array());
    for (const Variant &section_item : sections) {
        if (section_item.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary section = section_item;
        auto *rows = add<VBoxContainer>(guide);
        rows->add_theme_constant_override("separation", pixels(rows, spacing::md));
        guide_copy(rows, section.get("title", String()), LabelStyle::TITLE);
        guide_copy(rows, section.get("description", String()), LabelStyle::MUTED);
        const Array items = section.get("items", Array());
        for (const Variant &item_value : items) {
            if (item_value.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary item = item_value;
            const int token = style_spacing(item.get("token", String()));
            auto *row = add<BoxContainer>(rows);
            if (token == 0) {
                responsive_rows.append(row);
            }
            row->add_theme_constant_override("separation", pixels(row, spacing::md));
            auto *caption = label(row, item.get("label", String()), LabelStyle::HEADING);
            caption->set_custom_minimum_size(Vector2(pixels(row, token == 0 ? 150 : 52), 0));
            caption->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
            caption->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
            if (token > 0) {
                auto *length = label(row, String::num_int64(token) + " px", LabelStyle::HEADING);
                length->set_custom_minimum_size(Vector2(pixels(row, 42), 0));
                length->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
                guide_spacing_mark(row, token);
                auto *usage = label(row, item.get("description", String()));
                usage->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
                usage->set_h_size_flags(Control::SIZE_EXPAND_FILL);
                continue;
            }
            auto *details = add<VBoxContainer>(row);
            details->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            details->add_theme_constant_override("separation", pixels(details, spacing::xs));
            const String swatch = item.get("swatch", String());
            if (!swatch.is_empty()) {
                auto *chip_row = add<HBoxContainer>(details);
                chip_row->add_theme_constant_override("separation", pixels(chip_row, spacing::md));
                auto *chip = add<PanelContainer>(chip_row);
                chip->set_custom_minimum_size(Vector2(pixels(chip, 20), pixels(chip, 14)));
                chip->add_theme_stylebox_override("panel", rounded_box(Color::html(swatch), color(Surface::OUTLINE), 0, 0,
                        chip->get_theme_default_base_scale(), 4));
                guide_copy(chip_row, swatch, LabelStyle::HEADING);
            } else {
                guide_copy(details, item.get("value", String()), LabelStyle::HEADING);
            }
            guide_copy(details, item.get("description", String()));
            guide_specimen(details, item.get("specimen", String()));
        }
    }
    page->connect("resized", callable_mp_static(fit_guide_rows).bind(page, responsive_rows));
    fit_guide_rows(page, responsive_rows);
}

/// Write the selected command to its nearby demo status.
void status(Label *label, String message) {
    label->set_text(message);
}

/// Write a tree selection without changing its data.
void selected(StringName id, Label *report) {
    report->set_text("Selected: " + String(id));
}

/// Add a display-only icon for a sample card, item row or picker option.
TextureRect *media(Node *theme_source, const StringName &symbol) {
    auto *image = add<TextureRect>(nullptr);
    image->set_texture(icon(theme_source, symbol));
    image->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
    image->set_custom_minimum_size(Vector2(pixels(theme_source, 20), pixels(theme_source, 20)));
    image->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    return image;
}

/// Give a representative content slot visible text and a title.
VBoxContainer *text_section(const String &title, const String &body) {
    auto *slot = add<VBoxContainer>(nullptr);
    label(slot, title, LabelStyle::HEADING);
    label(slot, body, LabelStyle::MUTED);
    return slot;
}

/// Build one sample slot with a left or right pin and its caption.
void graph_sample_slot(EditorGraphNode *node, const String &caption, bool input, bool execution, bool filled, bool variable_name = false) {
    const int slot = node->get_child_count();
    auto *row = add<HBoxContainer>(node);
    row->set_custom_minimum_size(Vector2(0, pixels(node, variable_name ? graph_compact_read_height : graph_node_port_row_height)));
    row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    if (show_graph_pin_label(caption, execution)) {
        auto *caption_label = label(row, caption);
        caption_label->set_theme_type_variation(variable_name ? "GraphCompactRead" : execution ? "GraphFlowPin" : "GraphDataPin");
        if (variable_name) {
            caption_label->set_name("GraphVariableName");
        }
        caption_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        caption_label->set_horizontal_alignment(input || variable_name ? HORIZONTAL_ALIGNMENT_LEFT : HORIZONTAL_ALIGNMENT_RIGHT);
    }
    const int type = execution ? 0 : 1;
    const Color tint = execution ? graph_execution_color() : graph_pin_color(Variant::FLOAT);
    node->set_slot(slot, input, type, tint, !input, type, tint, {}, {}, false);
    node->set_slot_connected(slot, input, filled);
}

/// Attach a reusable node card and place it at an editor-scaled sample location.
EditorGraphNode *graph_sample_node(GraphEdit *graph, const String &title, const Vector2 &position, GraphNodeHeader role) {
    auto *node = add<EditorGraphNode>(graph);
    node->set_title(title);
    node->set_position_offset(Vector2(pixels(graph, position.x), pixels(graph, position.y)));
    node->configure(role);
    return node;
}

/// Create a miniature native graph for an independently scrolling catalog specimen.
GraphEdit *graph_example_canvas(Node *owner, const StringName &name) {
    auto *graph = add<GraphEdit>(owner);
    graph->set_name(name);
    graph->set_custom_minimum_size(Vector2(0, pixels(graph, 220)));
    graph->set_show_menu(false);
    graph->set_minimap_enabled(false);
    style_graph_canvas(graph);
    graph->set_connection_lines_thickness(1.7);
    graph->set_connection_lines_curvature(0.36);
    return graph;
}

/// Show native role cards, filled execution and data sockets, and a free execution socket.
void graph_node_example(Node *owner) {
    auto *graph = graph_example_canvas(owner, "CatalogGraphNodeSample");
    auto *source = graph_sample_node(graph, "On Hit", Vector2(10, 32), GraphNodeHeader::EVENT);
    graph_sample_slot(source, "Then", false, true, true);
    graph_sample_slot(source, "Damage", false, false, true);
    auto *target = graph_sample_node(graph, "Apply Damage", Vector2(222, 55), GraphNodeHeader::CALL);
    graph_sample_slot(target, "In", true, true, true);
    graph_sample_slot(target, "Amount", true, false, true);
    graph_sample_slot(target, "Then", false, true, false);
    const auto fit = +[](GraphEdit *canvas, EditorGraphNode *card) {
        const float apparent_width = std::max<float>(card->get_size().x, pixels(canvas, 170));
        const float x = std::clamp<float>(canvas->get_size().x - apparent_width - pixels(canvas, 8),
                pixels(canvas, 10), pixels(canvas, 222));
        const float y = pixels(canvas, x < pixels(canvas, 198) ? 106 : 55);
        card->set_position_offset(Vector2(x, y));
    };
    graph->connect("resized", callable_mp_static(fit).bind(graph, target));
    target->connect("resized", callable_mp_static(fit).bind(graph, target));
    fit(graph, target);
    graph->connect_node(source->get_name(), 0, target->get_name(), 0, true);
    graph->connect_node(source->get_name(), 1, target->get_name(), 1, true);
}

/// Show the same passive statuses used by an authored or paused graph.
void graph_status_example(Node *owner) {
    auto *graph = graph_example_canvas(owner, "CatalogGraphStatusSample");
    auto *disabled = graph_sample_node(graph, "Play Sound", Vector2(10, 55), GraphNodeHeader::CALL);
    graph_sample_slot(disabled, "In", true, true, false);
    graph_sample_slot(disabled, "Then", false, true, false);
    style_graph_node_disabled(disabled);
    auto *paused = graph_sample_node(graph, "Apply Damage", Vector2(222, 55), GraphNodeHeader::CALL);
    graph_sample_slot(paused, "In", true, true, false);
    graph_sample_slot(paused, "Amount", true, false, false);
    graph_sample_slot(paused, "Then", false, true, false);
    paused->set_paused(true);
}

/// Show typed headers and sockets for both forms of a variable card.
void graph_variable_example(Node *owner) {
    auto *graph = graph_example_canvas(owner, "CatalogGraphVariableSample");
    const Color tint = graph_pin_color(Variant::FLOAT);
    auto *getter = add<EditorGraphNode>(graph);
    getter->set_title("Get shield");
    getter->set_position_offset(Vector2(pixels(graph, 10), pixels(graph, 42)));
    getter->configure(GraphNodeHeader::VALUE, GraphNodeStyle::COMPACT_VALUE, tint);
    graph_sample_slot(getter, "shield", false, false, true, true);
    auto *setter = add<EditorGraphNode>(graph);
    setter->set_title("Set shield");
    setter->set_position_offset(Vector2(pixels(graph, 222), pixels(graph, 55)));
    setter->configure(GraphNodeHeader::VALUE, GraphNodeStyle::VARIABLE_SETTER, tint);
    graph_sample_slot(setter, "Value", true, false, true);
    graph->connect_node(getter->get_name(), 0, setter->get_name(), 0, true);
}

/// A live catalog backed by the same native factories available to editor tools.
class ComponentCatalogWindow final : public Window {
    GDCLASS(ComponentCatalogWindow, Window)

    /// Sidebar controls maintain one selected page.
    Ref<ButtonGroup> pages;
    /// Root of the selected component detail.
    VBoxContainer *detail = nullptr;
    /// Scrolling viewport for detail variants.
    ScrollContainer *scroller = nullptr;
    /// One or two columns host the current component variants.
    BoxContainer *examples = nullptr;
    /// Reusable form sample shown on the Dialog page.
    EditorModal *form_example = nullptr;
    /// Reusable destructive sample shown on the Dialog page.
    EditorModal *removal_example = nullptr;
    /// Editable field owned by the form sample.
    LineEdit *form_name = nullptr;
    /// First column of the current variants.
    VBoxContainer *first_column = nullptr;
    /// Second column, unused for full-width compositions.
    VBoxContainer *second_column = nullptr;
    /// Next variant position in the grid.
    int variant_index = 0;
    /// Give the wider compositions the full detail width.
    bool wide = false;

    /// Stack specimens when the window cannot provide two readable widths.
    void fit_specimens() {
        if (examples) {
            examples->set_vertical(!wide && get_size().x < pixels(this, 790));
        }
    }

    /// Dismiss a sample dialog before closing the catalog itself.
    void close_catalog() {
        for (auto *modal : {form_example, removal_example}) {
            if (modal->is_open()) {
                modal->cancel();
                return;
            }
        }
        hide();
    }

    /// Open the catalog example through the normal live form composition.
    void open_form_example() {
        form_name->set_text("Training Room");
        form_example->open("Rename input map", {}, form_name, 430);
        form_name->select_all();
    }

    /// Open the catalog example through the normal destructive confirmation composition.
    void open_removal_example() {
        removal_example->open("Remove \"Jump\"?", "This action and its bindings will be removed from the input map.");
    }

    /// Add a named variant surface and return the detached content parent.
    VBoxContainer *variant(const String &title) {
        auto *contents = add<VBoxContainer>(nullptr);
        contents->add_theme_constant_override("separation", pixels(this, 6));
        VBoxContainer *column = wide || (variant_index++ % 2 == 0) ? first_column : second_column;
        auto *caption = label(nullptr, title, LabelStyle::HEADING);
        card(column, caption, contents, nullptr, color(Surface::RAISED));
        return contents;
    }

    /// Populate the selected control page using live sample variants.
    void controls(Component component) {
        switch (component) {
            case BUTTON: {
                const char *names[] = {"Default", "Primary", "Quiet", "Destructive", "Icon only", "Disabled"};
                for (int index = 0; index < 6; ++index) {
                    auto *sample = variant(names[index]);
                    auto *report = label(sample, {}, LabelStyle::MUTED);
                    Button *control;
                    if (index == 4) {
                        control = icon_button(sample, "add", "Add action", callable_mp_static(status).bind(report, "Action added."));
                    } else {
                        const bool destructive = index == 3;
                        const ButtonStyle styles[] = {ButtonStyle::SECONDARY, ButtonStyle::PRIMARY, ButtonStyle::QUIET, ButtonStyle::DESTRUCTIVE};
                        control = button(sample, destructive ? "Remove action" : index == 2 ? "Cancel" : "Save action", {},
                                callable_mp_static(status).bind(report, destructive ? "Action removed." : index == 2 ? "Canceled." : "Action saved."),
                                styles[index < 4 ? index : 0]);
                    }
                    sample->move_child(report, 1);
                    control->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                    if (index == 5) {
                        control->set_disabled(true);
                        control->set_focus_mode(Control::FOCUS_NONE);
                        control->set_default_cursor_shape(Control::CURSOR_FORBIDDEN);
                    }
                }
            } break;
            case TEXT_INPUT:
                text_input(variant("Default"), "Interact", "Action name");
                search_input(variant("Search"), "Search actions", "Find an input action.");
                text_input(variant("Read-only"), "Interact", "Action name", FieldState::READ_ONLY);
                {
                    auto *invalid = variant("Invalid");
                    text_input(invalid, {}, "Enter an action name.", FieldState::INVALID);
                    label(invalid, "Enter an action name.", LabelStyle::MUTED)->add_theme_color_override("font_color", color(Surface::DANGER));
                }
                text_input(variant("Disabled"), "Interact", "Action name", FieldState::DISABLED);
                break;
            case TEXT_AREA:
                text_area(variant("Default"), "Use the focused object.", "Action description");
                text_area(variant("Read-only"), "Use the focused object.", "Action description", FieldState::READ_ONLY);
                text_area(variant("Disabled"), "Use the focused object.", "Action description", FieldState::DISABLED);
                break;
            case NUMERIC_INPUT:
                number_input(variant("Default"), "sensitivity", 1.25, 0, 4, 0.05, {}, FieldState::EDITABLE, "Sensitivity from 0 to 4.");
                number_input(variant("With a unit"), "sensitivity", 1.25, 0, 4, 0.05)->set_unit(String(U"\u00D7"));
                number_input(variant("Read-only"), "sensitivity", 1.25, 0, 4, 0.05, {}, FieldState::READ_ONLY);
                {
                    auto *invalid = variant("Invalid");
                    number_input(invalid, "sensitivity", 1.25, 0, 4, 0.05, {}, FieldState::INVALID, "Enter a number from 0 to 4.")->set_invalid_draft("5");
                    label(invalid, "Enter a value from 0 to 4.", LabelStyle::MUTED)->add_theme_color_override("font_color", color(Surface::DANGER));
                }
                number_input(variant("Disabled"), "sensitivity", 1.25, 0, 4, 0.05, {}, FieldState::DISABLED);
                break;
            case SLIDER:
                slider(variant("Default"), "Dead zone", 34, 0, 100, 1, {}, FieldState::EDITABLE, "%");
                slider(variant("At the lower bound"), "Dead zone", 0, 0, 100, 1, {}, FieldState::EDITABLE, "%");
                slider(variant("Disabled"), "Dead zone", 64, 0, 100, 1, {}, FieldState::DISABLED, "%");
                break;
            case CHECKBOX:
                checkbox(variant("Unchecked"), "Include gamepad bindings");
                checkbox(variant("Checked"), "Include gamepad bindings", CheckState::CHECKED);
                checkbox(variant("Mixed"), "Include gamepad bindings", CheckState::MIXED);
                checkbox(variant("Disabled"), "Include gamepad bindings", CheckState::UNCHECKED, {}, true);
                break;
            case SWITCH:
                toggle_switch(variant("Off"), "Live input", false);
                toggle_switch(variant("On"), "Live input", true);
                toggle_switch(variant("Disabled"), "Live input", false, {}, true);
                break;
            case RADIO:
                for (const bool disabled : {false, true}) {
                    auto *row = add<HBoxContainer>(variant(disabled ? "Disabled" : "Default"));
                    Ref<ButtonGroup> group;
                    group.instantiate();
                    radio(row, "Button", group, true, {}, disabled);
                    radio(row, "Axis", group, false, {}, disabled);
                    radio(row, "Vector", group, false, {}, disabled);
                }
                break;
            case SELECT: {
                const PackedStringArray simple = {"Pressed", "Released", "Held"};
                auto sample_width = [this](Control *control) {
                    control->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                    control->set_custom_minimum_size(Vector2(pixels(this, 280), 0));
                };
                auto *plain = select(variant("Text options"), simple, 0);
                plain->set_custom_minimum_size(Vector2(pixels(this, 117), pixels(this, 24)));
                plain->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                auto *rich = picker(variant("Rich options"), "Choose a device");
                sample_width(rich);
                auto rich_device = [this](const char *title, const char *description, const char *symbol) {
                    return item_row(nullptr, title, description, media(this, symbol));
                };
                rich->set_options({
                    {"keyboard", "Keyboard & mouse", {}, rich_device("Keyboard & mouse", "Keys, pointer motion, and scroll", "keyboard"),
                            rich_device("Keyboard & mouse", "Keys, pointer motion, and scroll", "keyboard")},
                    {"gamepad", "Gamepad", {}, rich_device("Gamepad", "Buttons, sticks, and analog triggers", "gamepad"),
                            rich_device("Gamepad", "Buttons, sticks, and analog triggers", "gamepad")},
                    {"analog", "Analog axes", {}, rich_device("Analog axes", "Continuous 1D or 2D values", "axes"),
                            rich_device("Analog axes", "Continuous 1D or 2D values", "axes")}
                });
                rich->set_selected("keyboard");
                auto *readonly = select(variant("Read-only"), simple, 0, {}, FieldState::READ_ONLY);
                readonly->set_custom_minimum_size(Vector2(pixels(this, 117), pixels(this, 24)));
                readonly->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                auto *disabled = select(variant("Disabled"), simple, 0, {}, FieldState::DISABLED);
                disabled->set_custom_minimum_size(Vector2(pixels(this, 117), pixels(this, 24)));
                disabled->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
            } break;
            case COMBOBOX:
                for (const char *name : {"Interactive", "Disabled"}) {
                    auto *sample = variant(name);
                    auto *searchable = picker(sample, "Find an action", {}, true);
                    searchable->set_popup_width_limit(328);
                    searchable->set_options({{"move", "Move", "Movement"}, {"jump", "Jump", "Movement"},
                            {"look", "Look", "View"}, {"interact", "Interact", "Gameplay"}});
                    if (String(name) == "Disabled") {
                        searchable->set_state(FieldState::DISABLED);
                    }
                }
                break;
            case DISCLOSURE:
                disclosure(variant("Collapsed"), "Advanced options", label(nullptr, "Changes apply to the current action."), false);
                disclosure(variant("Expanded"), "Advanced options", label(nullptr, "Changes apply to the current action."), true);
                break;
            case TABS:
                for (int index = 0; index < 2; ++index) {
                    auto *control = tabs(variant(index == 0 ? "Device controls" : "Alternate selection"));
                    auto *first = label(control, "Physical key codes");
                    auto *second = label(control, "Controller button codes");
                    first->set_name("Keyboard");
                    second->set_name("Gamepad");
                    control->set_current_tab(index);
                }
                break;
            case MENU: {
                auto *sample = variant("Interactive");
                auto *control = menu(sample, "Actions", "Open the available commands.");
                control->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                auto *report = label(sample, {}, LabelStyle::MUTED);
                auto *popup = control->get_popup();
                popup->add_item("Duplicate", -1, static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_D));
                popup->add_separator();
                popup->add_item("Reset to defaults");
                popup->add_item("Restore previous binding");
                popup->set_item_disabled(3, true);
                popup->connect("index_pressed", callable_mp_static(+[](int64_t index, Label *message) {
                    message->set_text(index == 0 ? "Duplicate selected." : "Reset to defaults selected.");
                }).bind(report));

                auto *copy = add<PopupMenu>(popup);
                copy->set_theme(popup->get_theme());
                copy->add_item("Name");
                copy->add_item("Resource path");
                copy->connect("index_pressed", callable_mp_static(+[](int64_t index, Label *message) {
                    message->set_text(index == 0 ? "Copy name selected." : "Copy resource path selected.");
                }).bind(report));
                popup->add_submenu_node_item("Copy as", copy);

                auto *disabled = menu(variant("Disabled"), "Actions");
                disabled->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                disabled->set_disabled(true);
                disabled->set_focus_mode(Control::FOCUS_NONE);
                disabled->set_default_cursor_shape(Control::CURSOR_FORBIDDEN);
            } break;
            case TOOLTIP: {
                auto *help = button(variant("Help text"), "?", "Ignore small movements near the stick's resting position.");
                help->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                auto *options = button(variant("Keyboard shortcut"), "Display options", "Show display options",
                        {}, ButtonStyle::SECONDARY, static_cast<Key>(KEY_MASK_SHIFT | KEY_V));
                options->set_name("CatalogShortcutTooltipTrigger");
                options->set_accessibility_name("Display options");
                options->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                break;
            }
            case PROGRESS:
                progress(variant("Ready"), "Import progress", 0);
                progress(variant("In progress"), "Import progress", 40);
                progress(variant("Complete"), "Import progress", 100);
                progress(variant("Indeterminate"), "Importing", 0, true);
                break;
            default: break;
        }
    }

    /// Build two independent trees backed by the same row and selection API.
    void tree_examples() {
        auto *bindings = variant("Action bindings");
        auto *binding_report = label(bindings, "Selected: Left Stick", LabelStyle::MUTED);
        auto *actions = tree(bindings, callable_mp_static(selected).bind(binding_report));
        bindings->move_child(binding_report, 1);
        auto *move = add<HBoxContainer>(nullptr);
        label(move, "Move", LabelStyle::HEADING);
        label(move, "2D Vector", LabelStyle::MUTED);
        actions->add_row("Move", move);
        for (const char *name : {"W", "A", "S", "D", "Left Stick"}) {
            auto *row = add<HBoxContainer>(nullptr);
            row->add_theme_constant_override("separation", pixels(this, 6));
            auto *device = icon_button(row, String(name) == "Left Stick" ? "gamepad" : "keyboard", "Input device", {}, ButtonStyle::QUIET);
            device->set_custom_minimum_size(Vector2(pixels(this, 28), pixels(this, 28)));
            auto *choice = picker(row, name);
            choice->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            choice->get_trigger()->set_theme_type_variation("CrimsonQuiet");
            choice->set_options({{name, name}, {"Q", "Q"}, {"E", "E"}, {"Space Bar", "Space Bar"}});
            choice->set_selected(name);
            auto *remove = icon_button(nullptr, "remove", "Remove binding",
                    callable_mp_static(status).bind(binding_report, "Remove " + String(name)), ButtonStyle::QUIET);
            remove->set_custom_minimum_size(Vector2(pixels(this, 28), pixels(this, 28)));
            actions->add_row(name, row, "Move", remove);
        }
        actions->add_row("Jump", label(nullptr, "Jump   Button", LabelStyle::HEADING), {}, nullptr, false);
        actions->add_row("Space Bar", label(nullptr, "Space Bar"), "Jump");
        actions->set_selected("Left Stick");

        auto *assets = variant("Asset folders");
        auto *asset_report = label(assets, "Selected: On Foot", LabelStyle::MUTED);
        auto *folders = tree(assets, callable_mp_static(selected).bind(asset_report));
        assets->move_child(asset_report, 1);
        folders->add_row("Input Maps", item_row(nullptr, "Input Maps", {}, media(this, "folder")));
        folders->add_row("On Foot", label(nullptr, "On Foot   Input Map"), "Input Maps");
        folders->add_row("Vehicle", label(nullptr, "Vehicle   Input Map"), "Input Maps");
        folders->add_row("Presets", item_row(nullptr, "Presets", {}, media(this, "folder")), {}, nullptr, false);
        folders->add_row("Gamepad", label(nullptr, "Gamepad   Device Preset"), "Presets");
        folders->set_selected("On Foot");
    }

    /// Populate the selected composition page with independent content slots.
    void compositions(Component component) {
        switch (component) {
            case TREE: tree_examples(); break;
            case CARD:
                card(variant("Inspector card"), label(nullptr, "Dead Zone", LabelStyle::HEADING),
                        text_section("Radial", "Ignore movement close to the resting stick position."));
                card(variant("Tinted card with an action"), label(nullptr, "Input map", LabelStyle::HEADING),
                        text_section("On Foot", "Bindings shared by the player controls."),
                        button(nullptr, "Open map", "Inspect this resource."), color(Surface::SELECTED));
                break;
            case FIELD_GROUP: {
                auto *group = field_group(variant("Stacked fields"), "Binding settings");
                label(group, "Action name", LabelStyle::MUTED);
                text_input(group, "Move", "Action name");
                label(group, "Value type", LabelStyle::MUTED);
                select(group, PackedStringArray{"2D Vector", "Button"}, 0);
                auto *inline_group = field_group(variant("Inline inspector fields"), "Dead Zone");
                field(inline_group, "Mode", select(nullptr, PackedStringArray{"Radial", "Axial"}, 0), "Choose the dead zone shape.");
                field(inline_group, "Lower threshold", number_input(nullptr, "lower threshold", 0.15, 0, 1, 0.01), "Ignore inputs below this value.");
            } break;
            case EMPTY_STATE: {
                auto *sample = variant("New input map");
                auto *report = label(sample, {}, LabelStyle::MUTED);
                empty_state(sample, "No bindings", "Add a binding to begin.", button(nullptr, "Add binding", {},
                        callable_mp_static(status).bind(report, "Binding added.")), media(this, "keyboard"));
                sample->move_child(report, 1);
                empty_state(variant("No search results"), "No matching controls", "Try a different search.", nullptr, media(this, "search"));
            } break;
            case ITEM_ROW: {
                auto *sample = variant("Linked resource");
                auto *report = label(sample, {}, LabelStyle::MUTED);
                item_row(sample, "On Foot", "Input Map", media(this, "file"),
                        button(nullptr, "Open", "Open the linked input map.", callable_mp_static(status).bind(report, "Opened On Foot.")));
                sample->move_child(report, 1);
                item_row(variant("Missing reference"), "Missing input map", "The resource was moved or removed.", media(this, "file"),
                        button(nullptr, "Relink", "Choose the replacement resource."));
            } break;
            case SPLIT_PANE: {
                auto *left = text_section("Actions", "Move, Jump, Interact");
                auto *right = text_section("Modifiers", "Select a binding to edit it.");
                left->set_h_size_flags(Control::SIZE_EXPAND_FILL);
                right->set_h_size_flags(Control::SIZE_EXPAND_FILL);
                split_pane(variant("Side by side"), left, right)->set_custom_minimum_size(Vector2(0, pixels(this, 86)));
                auto *top = text_section("Preview", "Input map");
                auto *bottom = text_section("Details", "Asset path and type");
                top->set_v_size_flags(Control::SIZE_EXPAND_FILL);
                bottom->set_v_size_flags(Control::SIZE_EXPAND_FILL);
                split_pane_vertical(variant("Stacked panels"), top, bottom)->set_custom_minimum_size(Vector2(0, pixels(this, 110)));
            } break;
            case GRAPH_NODE:
                graph_node_example(variant("Event and function"));
                graph_variable_example(variant("Read and write a variable"));
                graph_status_example(variant("Disabled and paused"));
                break;
            case DIALOG:
                button(variant("Form"), "Open form", {}, callable_mp(this, &ComponentCatalogWindow::open_form_example))
                        ->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                button(variant("Destructive confirmation"), "Confirm removal", {},
                        callable_mp(this, &ComponentCatalogWindow::open_removal_example), ButtonStyle::DESTRUCTIVE)
                        ->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
                break;
            default: break;
        }
    }

    /// Replace the detail pane with every specimen of the chosen component.
    void show_component(int index) {
        if (index < 0 || index >= COMPONENT_COUNT) {
            return;
        }
        while (detail->get_child_count() > 0) {
            examples = nullptr;
            Node *child = detail->get_child(0);
            detail->remove_child(child);
            child->queue_free();
        }
        scroller->set_v_scroll(0);
        const auto component = static_cast<Component>(index);
        auto *heading = add<VBoxContainer>(detail);
        heading->add_theme_constant_override("separation", pixels(heading, 4));
        label(heading, component_names[index], LabelStyle::TITLE);
        auto *description = label(heading, component_descriptions[index], LabelStyle::MUTED);
        description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        examples = add<BoxContainer>(detail);
        examples->add_theme_constant_override("separation", pixels(examples, spacing::lg));
        first_column = add<VBoxContainer>(examples);
        first_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        first_column->add_theme_constant_override("separation", pixels(first_column, spacing::lg));
        second_column = add<VBoxContainer>(examples);
        second_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        second_column->add_theme_constant_override("separation", pixels(second_column, spacing::lg));
        wide = component >= TREE;
        second_column->set_visible(!wide);
        fit_specimens();
        variant_index = 0;
        if (wide) {
            compositions(component);
        } else {
            controls(component);
        }
    }

protected:
    /// The showcase is internal editor tooling.
    static void _bind_methods() {}

public:
    /// Build the catalog under its own theme after attaching it to the editor.
    void build() {
        hide();
        embed_editor_popups(this);
        set_name("CrimsonComponentCatalog");
        set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
        set_title("Crimson Editor Components");
        set_min_size(Vector2i(640, 480));
        const double scale = EditorInterface::get_singleton()->get_editor_scale();
        auto theme = create_theme(this, scale);
        style_graph_canvas(theme, scale);
        set_theme(theme);
        connect("close_requested", callable_mp(this, &ComponentCatalogWindow::close_catalog));
        connect("size_changed", callable_mp(this, &ComponentCatalogWindow::fit_specimens));
        auto *frame = surface_panel(this, Surface::CANVAS);
        frame->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
        auto *layout = add<VBoxContainer>(frame);
        layout->add_theme_constant_override("separation", 0);
        auto *tab_inset = add<MarginContainer>(layout);
        tab_inset->set_v_size_flags(Control::SIZE_EXPAND_FILL);
        padding(tab_inset, spacing::xl, 0);
        auto *navigation = tabs(tab_inset);
        navigation->set_name("CatalogNavigation");
        navigation->set_v_size_flags(Control::SIZE_EXPAND_FILL);
        auto *workspace = add<HBoxContainer>(navigation);
        workspace->set_name("Components");
        workspace->add_theme_constant_override("separation", pixels(this, 1));
        workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
        auto *sidebar = surface_panel(workspace, Surface::SIDEBAR);
        sidebar->set_custom_minimum_size(Vector2(pixels(this, 182), 0));
        auto *sidebar_scroll = add<ScrollContainer>(sidebar);
        sidebar_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
        sidebar_scroll->set_follow_focus(true);
        auto *sidebar_inset = add<MarginContainer>(sidebar_scroll);
        sidebar_inset->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        padding(sidebar_inset, spacing::md, spacing::lg);
        auto *items = add<VBoxContainer>(sidebar_inset);
        items->add_theme_constant_override("separation", pixels(this, spacing::xxs));
        pages.instantiate();
        for (int index = 0; index < COMPONENT_COUNT; ++index) {
            if (index == 0 || index == TREE) {
                auto *section = add<MarginContainer>(items);
                section->add_theme_constant_override("margin_left", pixels(this, spacing::md));
                section->add_theme_constant_override("margin_top", pixels(this, index == TREE ? spacing::lg : spacing::xxs));
                section->add_theme_constant_override("margin_bottom", pixels(this, spacing::xs));
                label(section, index == 0 ? "Controls" : "Compositions", LabelStyle::MUTED);
            }
            auto *entry = button(items, component_names[index], {}, callable_mp(this, &ComponentCatalogWindow::show_component).bind(index), ButtonStyle::ROW);
            entry->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
            entry->set_toggle_mode(true);
            entry->set_button_group(pages);
            entry->set_custom_minimum_size(Vector2(0, pixels(this, 28)));
            entry->add_theme_color_override("font_color", color(Surface::MUTED));
            entry->set_pressed_no_signal(index == 0);
        }
        auto *work_surface = surface_panel(workspace, Surface::HEADER);
        work_surface->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        scroller = add<ScrollContainer>(work_surface);
        scroller->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
        scroller->set_follow_focus(true);
        auto *inset = add<MarginContainer>(scroller);
        inset->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        padding(inset, spacing::xl, spacing::xl);
        detail = add<VBoxContainer>(inset);
        detail->add_theme_constant_override("separation", pixels(detail, spacing::xl));
        build_design_style(navigation);
        form_example = editor_modal(this, frame, {});
        form_example->set_name("CatalogFormDialog");
        form_example->set_confirm_text("Apply");
        form_name = text_input(nullptr, {}, "The friendly name shown in the Input Map editor.");
        field(form_example->body(), "Display name", form_name, form_name->get_tooltip_text());
        form_example->register_text_enter(form_name);
        removal_example = removal_confirmation(this, frame, {});
        show_component(BUTTON);
    }
};

/// Installs a tool entry and owns the floating catalog for the editor session.
class ComponentCatalogPlugin final : public EditorPlugin {
    GDCLASS(ComponentCatalogPlugin, EditorPlugin)

    /// The editor tree owns the preview window and all of its sample controls.
    ComponentCatalogWindow *catalog = nullptr;

    /// Open the catalog with space to compare component states.
    void open() {
        catalog->popup_centered_clamped(Vector2i(pixels(catalog, 1120), pixels(catalog, 825)), 0.90);
    }

protected:
    /// The menu hook exposes no game API.
    static void _bind_methods() {}

    /// Register and remove the menu with the editor plug-in lifecycle.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            catalog = memnew(ComponentCatalogWindow);
            catalog->hide();
            add_child(catalog);
            catalog->build();
            add_tool_menu_item("Editor Components", callable_mp(this, &ComponentCatalogPlugin::open));
        } else if (what == NOTIFICATION_EXIT_TREE) {
            remove_tool_menu_item("Editor Components");
            catalog = nullptr;
        }
    }
};

} // namespace

void register_editor_catalog() {
    GDREGISTER_INTERNAL_CLASS(ComponentCatalogWindow);
    GDREGISTER_INTERNAL_CLASS(ComponentCatalogPlugin);
}

void install_editor_catalog() {
    EditorPlugins::add_by_type<ComponentCatalogPlugin>();
}

void remove_editor_catalog() {
    EditorPlugins::remove_by_type<ComponentCatalogPlugin>();
}

} // namespace crimson::editor_ui

#endif
