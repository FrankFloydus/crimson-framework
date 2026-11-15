// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <optional>
#include <set>

namespace godot {
class Control;
class GraphEdit;
class TextureRect;
}

namespace crimson::editor_ui {

/// Shape of a native graph node card.
enum class GraphNodeStyle { STANDARD, COMPACT_VALUE, VARIABLE_SETTER, REROUTE };

/// Semantic titlebar color for a standard graph card.
enum class GraphNodeHeader { CONTROL, EVENT, CALL, VALUE, CUSTOM_CODE };

/// Glyph shown on a value pin or beside a type name.
enum class GraphValueShape { SCALAR, ARRAY, DICTIONARY, ENUM };

/// Visual width of a wire routing dot in logical pixels.
inline constexpr int graph_reroute_width = 42;
/// Visual height of a wire routing dot in logical pixels.
inline constexpr int graph_reroute_height = 22;
/// Logical corner size of a standard node card.
inline constexpr int graph_node_corner_radius = 8;
/// Logical minimum height for a standard node's port row.
inline constexpr int graph_node_port_row_height = 26;
/// Logical height of a compact read, including its single native socket row.
inline constexpr int graph_compact_read_height = 27;
/// Full corner radius for the compact read shape.
inline constexpr int graph_compact_read_radius = 14;
/// Visual text size for authored port names below the node title.
inline constexpr int graph_pin_font_size = 11;
/// Minimum height of inline fields, before the spacing below them.
inline constexpr int graph_literal_height = 23;

/// Suppress the generic flow labels while retaining labels that identify a branch.
bool show_graph_pin_label(const godot::String &caption, bool execution);

/// Apply the text role and height for a native field embedded in a graph card.
void style_graph_inline_control(godot::Control *control, const godot::StringName &variation = {});

/// Apply native graph canvas and node-title roles to an existing editor theme.
void style_graph_canvas(const godot::Ref<godot::Theme> &theme, double scale);

/// Use the themed alignment grid independently from graph snapping and wire behavior.
void style_graph_canvas(godot::GraphEdit *canvas);

/// Resolve a semantic header color independently of a tool's node types.
godot::Color graph_node_header(GraphNodeHeader role);

/// Read the editor color for a typed value pin.
godot::Color graph_pin_color(godot::Variant::Type type, bool enumeration = false);

/// Resolve the visual form of a Godot type or a named integer enum.
GraphValueShape graph_value_shape(godot::Variant::Type type, bool enumeration = false);

/// Get a scaled colored type glyph for an editor row or a picker option.
godot::Ref<godot::Texture2D> graph_type_marker(godot::Control *host, godot::Variant::Type type,
        bool enumeration = false, int logical_size = 10);

/// Color a collection glyph using its element color or its ordered key and value colors.
godot::Ref<godot::Texture2D> graph_type_marker(godot::Control *host, GraphValueShape shape, godot::Color primary,
        std::optional<godot::Color> secondary = {}, int logical_size = 10);

/// Read the editor color for a control flow pin.
godot::Color graph_execution_color();

/// Apply the shared card layout while the tool supplies its rows, title and semantics.
void style_graph_node(godot::GraphNode *node, GraphNodeStyle style,
        godot::Color header_tint = graph_node_header(GraphNodeHeader::CONTROL), std::optional<godot::Color> value_ink = {});

/// Add a compact role icon using a stock editor texture.
void style_graph_node_title_icon(godot::GraphNode *node, const godot::StringName &icon_name, godot::Color accent);

/// Add a compact role icon with an authored texture. The returned control can update its texture in place.
godot::TextureRect *style_graph_node_title_icon(godot::GraphNode *node, const godot::Ref<godot::Texture2D> &texture,
        godot::Color accent, godot::Color modulation);

/// Paint a card frame without modifying any native content margins or ports.
void draw_graph_node_outline(godot::Control *host, const godot::Ref<godot::StyleBoxFlat> &outline,
        const godot::Color &ink, bool emphasized, double zoom);

/// Create the reusable transparent outline used by native card paint callbacks.
godot::Ref<godot::StyleBoxFlat> graph_node_outline(godot::Control *host, int corner = graph_node_corner_radius);

/// Dim and hatch a disabled card without adding a native port row.
void style_graph_node_disabled(godot::GraphNode *node);

/// Create the compact status pill once when a card first pauses.
godot::Ref<godot::StyleBoxFlat> graph_node_paused_badge(godot::Control *host);

/// Paint the paused status above the native card without adding an input target.
void draw_graph_node_paused(godot::Control *host, const godot::Ref<godot::StyleBoxFlat> &badge);

/// Read the outline color for the active instruction in a paused run.
godot::Color graph_node_paused_outline();

/// Draw a pin at its native hit position. Call only from the host's draw callback.
void draw_graph_pin(godot::Control *host, const godot::Vector2 &center, const godot::Color &ink,
        bool execution, bool connected, double zoom, GraphValueShape shape = GraphValueShape::SCALAR,
        std::optional<godot::Color> secondary = {});

/// A node card for tools that need native slots and shared rendering without a custom subclass.
class EditorGraphNode final : public godot::GraphNode {
    GDCLASS(EditorGraphNode, godot::GraphNode)

    /// Uses Godot slot indexes so drawing does not depend on port cache positions.
    std::set<int32_t> connected_inputs;
    /// Uses Godot slot indexes so drawing does not depend on port cache positions.
    std::set<int32_t> connected_outputs;
    /// Reusable frame for hover-independent selection paint.
    godot::Ref<godot::StyleBoxFlat> outline;
    /// Variable cards use their socket type for the idle frame.
    godot::Color idle_outline;
    /// A nonempty style identifies a specimen with the paused status.
    godot::Ref<godot::StyleBoxFlat> paused_badge;

protected:
    /// The editor-only card exposes no script bindings.
    static void _bind_methods() {}
    /// Repaint a standard card when its selection or scaled outline changes.
    void _notification(int what);

public:
    /// Use the native pin position and palette for execution and data sockets.
    void _draw_port(int32_t slot, const godot::Vector2i &position, bool left, const godot::Color &ink) override;

    /// Apply the card style and role after attaching the node to a themed canvas.
    void configure(GraphNodeHeader role, GraphNodeStyle style = GraphNodeStyle::STANDARD, godot::Color variable_type = {});
    /// Set the drawn connection state of a native slot. Port type 0 denotes execution.
    void set_slot_connected(int32_t slot, bool input, bool connected);
    /// Present or clear the active-instruction decoration on the card.
    void set_paused(bool paused);
};

/// Register the reusable node card at editor initialization.
void register_editor_graph();

} // namespace crimson::editor_ui

#endif
