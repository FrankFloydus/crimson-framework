// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "editor_graph.hpp"

#ifdef DEBUG_ENABLED

#include "editor_controls.hpp"

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/graph_edit.hpp>
#include <godot_cpp/classes/graph_node.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/style_box_texture.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace crimson::editor_ui {
using namespace godot;

void style_graph_canvas(const Ref<Theme> &theme, double scale) {
    const Color transparent(0, 0, 0, 0);
    const Color graphite = Color::html("#151516");
    const Color rule = color(Surface::RULE);
    theme->set_stylebox("panel", "GraphEdit", rounded_box(graphite, transparent, 0, 0, scale, popup_corner_radius));
    theme->set_stylebox("panel_focus", "GraphEdit", box(transparent, transparent, 0, 0, scale));
    theme->set_color("grid_major", "GraphEdit", graphite.lerp(rule, 0.88f));
    theme->set_color("grid_minor", "GraphEdit", graphite.lerp(rule, 0.31f));
    theme->set_constant("port_hotzone_inner_extent", "GraphEdit", std::lround(11 * scale));
    theme->set_constant("port_hotzone_outer_extent", "GraphEdit", std::lround(14 * scale));
    theme->set_type_variation("GraphNodeTitleLabel", "Label");
    theme->set_font("font", "GraphNodeTitleLabel", theme->get_font("font", "CrimsonHeading"));
    theme->set_font_size("font_size", "GraphNodeTitleLabel", std::lround(13 * scale));
    theme->set_color("font_color", "GraphNodeTitleLabel", color(Surface::TEXT));
    theme->set_color("font_outline_color", "GraphNodeTitleLabel", transparent);
    theme->set_constant("outline_size", "GraphNodeTitleLabel", 0);
    theme->set_type_variation("GraphCompactRead", "CrimsonHeading");
    theme->set_font_size("font_size", "GraphCompactRead", std::lround(12 * scale));
    theme->set_color("font_color", "GraphCompactRead", color(Surface::TEXT));
    theme->set_constant("outline_size", "GraphCompactRead", 0);
    for (const auto &[role, ink] : {std::pair{"GraphFlowPin", Surface::TEXT}, {"GraphDataPin", Surface::MUTED}}) {
        theme->set_type_variation(role, "Label");
        theme->set_font_size("font_size", role, std::lround(graph_pin_font_size * scale));
        theme->set_color("font_color", role, color(ink));
        theme->set_constant("outline_size", role, 0);
    }
    for (const auto &[variation, base] : {std::pair{"GraphLiteralText", "LineEdit"},
                 {"GraphLiteralSelect", "CrimsonSelect"},
                 {"GraphLiteralAsset", "Button"}, {"GraphNodeAction", "CrimsonQuiet"}}) {
        theme->set_type_variation(variation, base);
        theme->set_font_size("font_size", variation, std::lround(graph_pin_font_size * scale));
        theme->set_color("font_color", variation, color(Surface::MUTED));
    }
    const auto entry = rounded_box(color(Surface::CANVAS), color(Surface::OUTLINE), 6, 2, scale, 6);
    theme->set_stylebox("normal", "GraphLiteralText", entry);
    theme->set_color("font_placeholder_color", "GraphLiteralText", color(Surface::DISABLED));
}

void style_graph_canvas(GraphEdit *canvas) {
    canvas->set_grid_pattern(GraphEdit::GRID_PATTERN_LINES);
}

Color graph_node_header(GraphNodeHeader role) {
    switch (role) {
        case GraphNodeHeader::EVENT: return Color::html("#58303b");
        case GraphNodeHeader::CALL: return Color::html("#483456");
        case GraphNodeHeader::VALUE: return Color::html("#304934");
        case GraphNodeHeader::CONTROL: return Color::html("#214745");
        case GraphNodeHeader::CUSTOM_CODE: return Color::html("#263b60");
    }
    return color(Surface::RAISED);
}

Color graph_pin_color(Variant::Type type, bool enumeration) {
    if (enumeration) {
        return Color::html("#e497ce");
    }
    switch (type) {
        case Variant::BOOL: return Color::html("#e0636b");
        case Variant::INT: return Color::html("#69c9bf");
        case Variant::FLOAT: return Color::html("#9bcf6d");
        case Variant::STRING: return Color::html("#d775b7");
        case Variant::STRING_NAME: return Color::html("#b78dcf");
        case Variant::NODE_PATH: return Color::html("#c99acb");
        case Variant::OBJECT: return Color::html("#71a7e4");
        case Variant::ARRAY:
        case Variant::PACKED_BYTE_ARRAY:
        case Variant::PACKED_INT32_ARRAY:
        case Variant::PACKED_INT64_ARRAY:
        case Variant::PACKED_FLOAT32_ARRAY:
        case Variant::PACKED_FLOAT64_ARRAY:
        case Variant::PACKED_STRING_ARRAY:
        case Variant::PACKED_VECTOR2_ARRAY:
        case Variant::PACKED_VECTOR3_ARRAY:
        case Variant::PACKED_COLOR_ARRAY:
        case Variant::PACKED_VECTOR4_ARRAY: return Color::html("#9494ed");
        case Variant::DICTIONARY: return Color::html("#eca36e");
        case Variant::VECTOR2:
        case Variant::VECTOR2I:
        case Variant::VECTOR3:
        case Variant::VECTOR3I:
        case Variant::VECTOR4:
        case Variant::VECTOR4I: return Color::html("#e2bd62");
        case Variant::TRANSFORM2D:
        case Variant::TRANSFORM3D:
        case Variant::PROJECTION:
        case Variant::BASIS:
        case Variant::QUATERNION: return Color::html("#de9764");
        case Variant::RECT2:
        case Variant::RECT2I:
        case Variant::PLANE:
        case Variant::AABB: return Color::html("#e2bd62");
        case Variant::RID: return Color::html("#83b6cb");
        case Variant::CALLABLE: return Color::html("#c498e2");
        case Variant::SIGNAL: return Color::html("#e58c97");
        case Variant::COLOR: return Color::html("#ecc187");
        default: return Color::html("#a2a4ae");
    }
}

GraphValueShape graph_value_shape(Variant::Type type, bool enumeration) {
    if (enumeration) {
        return GraphValueShape::ENUM;
    }
    if (type == Variant::DICTIONARY) {
        return GraphValueShape::DICTIONARY;
    }
    return type == Variant::ARRAY || (type >= Variant::PACKED_BYTE_ARRAY && type < Variant::VARIANT_MAX)
            ? GraphValueShape::ARRAY : GraphValueShape::SCALAR;
}

namespace {

/// Lay out the individual marks without a backing plate or outside border.
template <typename Draw>
void draw_type_cells(GraphValueShape shape, Draw draw) {
    if (shape == GraphValueShape::ARRAY) {
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                draw(Rect2(0.10f + x * 0.47f, 0.10f + y * 0.47f, 0.33f, 0.33f), 0.08f, false);
            }
        }
    } else if (shape == GraphValueShape::DICTIONARY) {
        for (int row = 0; row < 2; ++row) {
            const float y = 0.18f + row * 0.40f;
            draw(Rect2(0.07f, y, 0.29f, 0.24f), 0.12f, false);
            draw(Rect2(0.49f, y, 0.44f, 0.24f), 0.12f, true);
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            const float y = 0.096f + i * 0.31f;
            draw(Rect2(0.10f, y, 0.186f, 0.186f), 0.093f, false);
            draw(Rect2(0.39f, y, 0.51f, 0.186f), 0.093f, false);
        }
    }
}

/// Rasterize the same antialiased mark for pickers, variable rows, and graph pins.
void paint_type_cell(const Ref<Image> &image, int size, Color ink, const Rect2 &cell, float rounding) {
    const Rect2 scaled(cell.position * size, cell.size * size);
    const Vector2 center = scaled.get_center();
    const float radius = rounding * size;
    const Vector2 core = scaled.size * 0.5f - Vector2(radius, radius);
    const Rect2 area = scaled.grow(1.0f).intersection(Rect2(0, 0, size, size));
    for (int y = Math::floor(area.position.y); y < Math::ceil(area.get_end().y); ++y) {
        for (int x = Math::floor(area.position.x); x < Math::ceil(area.get_end().x); ++x) {
            const Vector2 edge = (Vector2(x + 0.5f, y + 0.5f) - center).abs() - core;
            const real_t distance = Vector2(std::max<real_t>(0, edge.x), std::max<real_t>(0, edge.y)).length() +
                    std::min<real_t>(0, std::max(edge.x, edge.y)) - radius;
            ink.a = std::clamp<real_t>(0.5 - distance, 0, 1);
            if (ink.a > image->get_pixel(x, y).a) {
                image->set_pixel(x, y, ink);
            }
        }
    }
}

/// Keep generated type marks in the workbench theme so repeated redraws can reuse them.
Ref<Texture2D> type_marker_texture(Control *host, GraphValueShape shape, Color ink, int size,
        std::optional<Color> secondary = {}) {
    Ref<Theme> theme;
    for (Node *owner = host; owner && theme.is_null(); owner = owner->get_parent()) {
        if (auto *control = Object::cast_to<Control>(owner)) {
            theme = control->get_theme();
        } else if (auto *window = Object::cast_to<Window>(owner)) {
            theme = window->get_theme();
        }
    }
    const Color second_ink = secondary.value_or(ink);
    const StringName key = ink.to_html(false) + "_" + second_ink.to_html(false) + "_" +
            String::num_int64(static_cast<int>(shape)) + "_" + String::num_int64(size);
    if (theme.is_valid() && theme->has_icon(key, "CrimsonTypeMarkers")) {
        return theme->get_icon(key, "CrimsonTypeMarkers");
    }
    auto image = Image::create_empty(size, size, false, Image::FORMAT_RGBA8);
    if (shape == GraphValueShape::SCALAR) {
        paint_type_cell(image, size, ink, Rect2(0.06f, 0.06f, 0.88f, 0.88f), 0.44f);
    } else {
        draw_type_cells(shape, [&](const Rect2 &cell, float rounding, bool value_side) {
            paint_type_cell(image, size, value_side ? second_ink : ink, cell, rounding);
        });
    }
    const Ref<Texture2D> marker = ImageTexture::create_from_image(image);
    if (theme.is_valid()) {
        theme->set_icon(key, "CrimsonTypeMarkers", marker);
    }
    return marker;
}

} // namespace

Ref<Texture2D> graph_type_marker(Control *host, Variant::Type type, bool enumeration, int logical_size) {
    return graph_type_marker(host, graph_value_shape(type, enumeration), graph_pin_color(type, enumeration), {}, logical_size);
}

Ref<Texture2D> graph_type_marker(Control *host, GraphValueShape shape, Color primary, std::optional<Color> secondary,
        int logical_size) {
    return type_marker_texture(host, shape, primary, std::max(6, pixels(host, logical_size)), secondary);
}

Color graph_execution_color() {
    return Color::html("#ccced8");
}

bool show_graph_pin_label(const String &caption, bool execution) {
    return !caption.is_empty() && (!execution || (caption != "In" && caption != "Then"));
}

void style_graph_inline_control(Control *control, const StringName &variation) {
    if (!variation.is_empty()) {
        control->set_theme_type_variation(variation);
    }
    control->add_theme_font_size_override("font_size", pixels(control, graph_pin_font_size));
    for (const char *state : {"font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color", "font_focus_color"}) {
        control->add_theme_color_override(state, color(Surface::MUTED));
    }
    Vector2 minimum = control->get_custom_minimum_size();
    minimum.y = pixels(control, graph_literal_height);
    control->set_custom_minimum_size(minimum);
}

namespace {

/// Cache a native typed titlebar by color for the lifetime of its scaled workbench theme.
Ref<StyleBox> typed_titlebar(GraphNode *node, Color value_ink) {
    const auto *window = node->get_window();
    const Ref<Theme> theme = window ? window->get_theme() : Ref<Theme>();
    const StringName key = value_ink.to_html(false);
    if (theme.is_valid() && theme->has_stylebox(key, "CrimsonGraphTypedTitle")) {
        return theme->get_stylebox(key, "CrimsonGraphTypedTitle");
    }
    const int radius = pixels(node, graph_node_corner_radius);
    const int width = 2 * radius + 96;
    const int height = radius + 4;
    const auto bitmap = Image::create(width, height, false, Image::FORMAT_RGBA8);
    const Color neutral = color(Surface::RAISED);
    const Color start = neutral.lerp(value_ink, 0.23f);
    for (int x = 0; x < width; ++x) {
        const float progress = static_cast<float>(x) / static_cast<float>(width - 1);
        const int edge_x = std::min(x, width - x - 1);
        const float corner_x = std::max(0.0f, radius - edge_x - 0.5f);
        for (int y = 0; y < height; ++y) {
            Color sample = y == height - 1 ? color(Surface::RULE) : start.lerp(neutral, progress);
            const float corner_y = std::max(0.0f, radius - y - 0.5f);
            sample.a = std::clamp(radius + 0.5f - std::hypot(corner_x, corner_y), 0.0f, 1.0f);
            bitmap->set_pixel(x, y, sample);
        }
    }
    Ref<StyleBoxTexture> result;
    result.instantiate();
    result->set_texture(ImageTexture::create_from_image(bitmap));
    for (const Side edge : {SIDE_LEFT, SIDE_RIGHT}) {
        result->set_texture_margin(edge, radius);
        result->set_content_margin(edge, pixels(node, 12));
    }
    result->set_texture_margin(SIDE_TOP, radius);
    result->set_texture_margin(SIDE_BOTTOM, 1);
    result->set_content_margin(SIDE_TOP, pixels(node, 8));
    result->set_content_margin(SIDE_BOTTOM, pixels(node, 8));
    if (theme.is_valid()) {
        theme->set_stylebox(key, "CrimsonGraphTypedTitle", result);
    }
    return result;
}

} // namespace

void style_graph_node(GraphNode *node, GraphNodeStyle style, Color header_tint, std::optional<Color> value_ink) {
    const bool reroute = style == GraphNodeStyle::REROUTE;
    const bool getter = style == GraphNodeStyle::COMPACT_VALUE;
    const bool has_titlebar = !reroute && !getter;
    const Color transparent(0, 0, 0, 0);
    const Color body = color(Surface::RAISED);
    const double scale = node->get_theme_default_base_scale();
    const int width = reroute ? graph_reroute_width : getter ? 66 : 170;
    node->set_custom_minimum_size(Vector2(pixels(node, width), reroute ? pixels(node, graph_reroute_height) : 0));
    node->add_theme_constant_override("separation", 0);
    const Color panel_fill = getter && value_ink ? body.lerp(*value_ink, 0.06f) : body;
    const auto panel = box(reroute ? transparent : panel_fill, transparent, reroute ? 0 : 19,
            reroute || getter ? 0 : 8, scale);
    if (getter) {
        panel->set_content_margin(SIDE_LEFT, pixels(node, 12));
        panel->set_content_margin(SIDE_RIGHT, pixels(node, 19));
        panel->set_corner_radius_all(pixels(node, graph_compact_read_radius));
        auto *titlebar = node->get_titlebar_hbox();
        // GraphNode computes header height from the child even when the titlebar is hidden.
        if (auto *title = Object::cast_to<Control>(titlebar->get_child(0))) {
            title->hide();
        }
        titlebar->hide();
    }
    const auto header = box(has_titlebar ? header_tint : transparent, transparent,
            has_titlebar ? 12 : 0, has_titlebar ? 8 : 0, scale);
    if (has_titlebar) {
        const int radius = pixels(node, graph_node_corner_radius);
        panel->set_corner_radius(CORNER_BOTTOM_LEFT, radius);
        panel->set_corner_radius(CORNER_BOTTOM_RIGHT, radius);
        header->set_corner_radius(CORNER_TOP_LEFT, radius);
        header->set_corner_radius(CORNER_TOP_RIGHT, radius);
        panel->set_anti_aliased(true);
        header->set_anti_aliased(true);
    } else if (getter) {
        panel->set_anti_aliased(true);
    }
    node->add_theme_constant_override("port_h_offset", reroute ? pixels(node, graph_reroute_width) / 2 - pixels(node, 3) : pixels(node, 12));
    if (reroute) {
        node->add_theme_stylebox_override("slot_selected", header);
    } else if (has_titlebar) {
        header->set_border_width(SIDE_BOTTOM, pixels(node, 1));
        header->set_border_color(color(Surface::RULE));
    }
    node->add_theme_stylebox_override("panel", panel);
    node->add_theme_stylebox_override("panel_selected", panel);
    const Ref<StyleBox> titlebar = has_titlebar && value_ink ? typed_titlebar(node, *value_ink) : Ref<StyleBox>(header);
    node->add_theme_stylebox_override("titlebar", titlebar);
    node->add_theme_stylebox_override("titlebar_selected", titlebar);
}

void style_graph_node_title_icon(GraphNode *node, const StringName &icon_name, Color accent) {
    if (icon_name.is_empty() || !node->has_theme_icon(icon_name, "EditorIcons")) {
        return;
    }
    style_graph_node_title_icon(node, node->get_theme_icon(icon_name, "EditorIcons"), accent, accent);
}

TextureRect *style_graph_node_title_icon(GraphNode *node, const Ref<Texture2D> &texture, Color accent, Color modulation) {
    auto *titlebar = node->get_titlebar_hbox();
    titlebar->add_theme_constant_override("separation", pixels(node, 6));
    if (auto *title = Object::cast_to<Label>(titlebar->get_child(0))) {
        title->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    }
    auto *chip = memnew(PanelContainer);
    titlebar->add_child(chip);
    auto background = box(accent.lerp(color(Surface::HEADER), 0.84f), Color(0, 0, 0, 0), 0, 0,
            node->get_theme_default_base_scale());
    background->set_corner_radius_all(pixels(node, 4));
    background->set_anti_aliased(true);
    chip->add_theme_stylebox_override("panel", background);
    chip->set_custom_minimum_size(Vector2(pixels(node, 16), pixels(node, 16)));
    chip->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    chip->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);

    auto *glyph = memnew(TextureRect);
    chip->add_child(glyph);
    glyph->set_name("GraphNodeRoleIcon");
    glyph->set_texture(texture);
    glyph->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    glyph->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    glyph->set_custom_minimum_size(Vector2(pixels(node, 11), pixels(node, 11)));
    glyph->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
    glyph->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
    glyph->set_modulate(modulation);
    glyph->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    titlebar->move_child(chip, 0);
    return glyph;
}

void draw_graph_pin(Control *host, const Vector2 &center, const Color &ink, bool execution, bool connected, double zoom,
        GraphValueShape shape, std::optional<Color> secondary) {
    zoom = std::max(zoom, 0.01);
    const float size = static_cast<float>(std::max(static_cast<double>(pixels(host, 4)), 2.2 / zoom));
    const float stroke = static_cast<float>(std::max(1.35, 1.15 / zoom));
    const Color fill = connected ? ink : color(Surface::RAISED);
    if (!execution && shape != GraphValueShape::SCALAR) {
        const float span = size * 2.75f;
        const Vector2 origin = center - Vector2(span, span) * 0.5f;
        draw_type_cells(shape, [&](const Rect2 &cell, float, bool value_side) {
            const Color cell_ink = value_side ? secondary.value_or(ink) : ink;
            host->draw_rect(Rect2(origin + cell.position * span, cell.size * span), connected ? cell_ink : cell_ink.lerp(fill, 0.18f));
        });
        return;
    }
    if (!execution) {
        host->draw_circle(center, size + stroke, color(Surface::CANVAS), true, -1, true);
        host->draw_circle(center, size, fill, true, -1, true);
        host->draw_circle(center, size, ink, false, stroke, true);
        return;
    }
    const float arrow = size * 1.25f;
    PackedVector2Array contour;
    contour.append(center + Vector2(-arrow, -arrow));
    contour.append(center + Vector2(arrow * 0.1f, -arrow));
    contour.append(center + Vector2(arrow, 0));
    contour.append(center + Vector2(arrow * 0.1f, arrow));
    contour.append(center + Vector2(-arrow, arrow));
    host->draw_colored_polygon(contour, fill);
    contour.append(contour[0]);
    host->draw_polyline(contour, ink, stroke, true);
}

Ref<StyleBoxFlat> graph_node_outline(Control *host, int corner) {
    auto outline = box(Color(0, 0, 0, 0), color(Surface::OUTLINE), 0, 0, host->get_theme_default_base_scale());
    outline->set_draw_center(false);
    outline->set_anti_aliased(true);
    outline->set_corner_radius_all(pixels(host, corner));
    return outline;
}

void draw_graph_node_outline(Control *host, const Ref<StyleBoxFlat> &outline, const Color &ink, bool emphasized, double zoom) {
    Ref<StyleBoxFlat> frame = outline;
    const int32_t width = static_cast<int32_t>(std::ceil(pixels(host, emphasized ? 2 : 1) / std::clamp(zoom, 0.01, 1.0)));
    frame->set_border_width_all(width);
    frame->set_border_color(ink);
    host->draw_style_box(frame, Rect2(Vector2(), host->get_size()));
}

namespace {

/// Paint disabled state over the whole card without adding a native port row.
class DisabledGraphOverlay final : public Control {
    GDCLASS(DisabledGraphOverlay, Control)

    /// Clip the diagonal strokes inside the same rounded edge used by the node.
    static bool inside(const Vector2 &point, const Vector2 &size, real_t radius) {
        const Vector2 inset(radius, radius);
        return point.distance_squared_to(point.clamp(inset, size - inset)) <= radius * radius;
    }

protected:
    static void _bind_methods() {}

    void _notification(int what) {
        if (what == NOTIFICATION_RESIZED) {
            queue_redraw();
        }
        if (what != NOTIFICATION_DRAW) {
            return;
        }
        const real_t inset = pixels(this, 2);
        const Vector2 origin(inset, inset);
        const Vector2 area = get_size() - origin * 2;
        if (area.x <= 0 || area.y <= 0) {
            return;
        }
        const real_t radius = std::clamp<real_t>(pixels(this, graph_node_corner_radius) - inset, 0,
                std::min(area.x, area.y) * 0.5f);
        const real_t pitch = std::max(1, pixels(this, 9));
        PackedVector2Array stripes;
        for (real_t diagonal = pitch * 0.5f; diagonal < area.x + area.y; diagonal += pitch) {
            const real_t first_y = std::max<real_t>(0, diagonal - area.x);
            const real_t last_y = std::min(diagonal, area.y);
            const Vector2 first(diagonal - first_y, first_y);
            const Vector2 last(diagonal - last_y, last_y);
            const Vector2 middle = first.lerp(last, 0.5f);
            if (!inside(middle, area, radius)) {
                continue;
            }
            const auto edge = [&](const Vector2 &end) {
                Vector2 outer = end;
                Vector2 inner = middle;
                for (int step = 0; step < 6 && !inside(outer, area, radius); ++step) {
                    const Vector2 probe = outer.lerp(inner, 0.5f);
                    if (inside(probe, area, radius)) {
                        inner = probe;
                    } else {
                        outer = probe;
                    }
                }
                return (inside(outer, area, radius) ? outer : inner) + origin;
            };
            stripes.append(edge(first));
            stripes.append(edge(last));
        }
        if (!stripes.is_empty()) {
            draw_multiline(stripes, Color(0.78f, 0.81f, 0.88f, 0.16f), std::max<real_t>(1, pixels(this, 1.5)), true);
        }
    }
};

} // namespace

void style_graph_node_disabled(GraphNode *node) {
    node->set_modulate(Color(0.74, 0.74, 0.77));
    auto *overlay = memnew(DisabledGraphOverlay);
    overlay->set_name("GraphDisabledOverlay");
    node->add_child(overlay, false, Node::INTERNAL_MODE_BACK);
    overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    overlay->set_focus_mode(Control::FOCUS_NONE);
    auto *status = label(node->get_titlebar_hbox(), "Disabled", LabelStyle::MUTED);
    status->set_name("GraphNodeDisabled");
    status->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    status->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    status->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    status->add_theme_font_size_override("font_size", pixels(node, 10));
    const String description = node->get_accessibility_description();
    node->set_accessibility_description(description.is_empty() ? String("Disabled") : description + String(". Disabled"));
}

Ref<StyleBoxFlat> graph_node_paused_badge(Control *host) {
    return rounded_box(Color::html("#2b251d"), Color::html("#705a36"), 0, 0, host->get_theme_default_base_scale(), 11);
}

void draw_graph_node_paused(Control *host, const Ref<StyleBoxFlat> &badge) {
    const Ref<Font> font = host->get_theme_default_font();
    const int32_t font_size = pixels(host, 11);
    const real_t inset = pixels(host, 8);
    const real_t height = pixels(host, 22);
    const real_t top = -height - pixels(host, 3);
    const Vector2 size = font->get_string_size("Paused", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
    host->draw_style_box(badge, Rect2(Vector2(0, top), Vector2(size.x + 2 * inset, height)));
    host->draw_string(font, Vector2(inset, top + (height - size.y) * 0.5f + font->get_ascent(font_size)), "Paused",
            HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color::html("#e6c991"));
}

Color graph_node_paused_outline() {
    return Color::html("#ffc76a");
}

void EditorGraphNode::_notification(int what) {
    if (what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
        queue_redraw();
    } else if (what == NOTIFICATION_DRAW && outline.is_valid()) {
        const bool selected = is_selected();
        const bool paused = paused_badge.is_valid();
        const Color ink = paused ? graph_node_paused_outline() : selected ? color(Surface::HIGHLIGHT) : idle_outline;
        draw_graph_node_outline(this, outline, ink, selected || paused, get_scale().x);
        if (paused) {
            draw_graph_node_paused(this, paused_badge);
        }
    }
}

void EditorGraphNode::_draw_port(int32_t slot, const Vector2i &position, bool left, const Color &ink) {
    const auto &connected = left ? connected_inputs : connected_outputs;
    const bool execution = (left ? get_slot_type_left(slot) : get_slot_type_right(slot)) == 0;
    draw_graph_pin(this, position, ink, execution, connected.count(slot) != 0, get_scale().x);
}

void EditorGraphNode::configure(GraphNodeHeader role, GraphNodeStyle style, Color variable_type) {
    const bool getter = style == GraphNodeStyle::COMPACT_VALUE;
    const bool variable = getter || style == GraphNodeStyle::VARIABLE_SETTER;
    style_graph_node(this, style, graph_node_header(role), variable ? std::optional<Color>(variable_type) : std::nullopt);
    idle_outline = variable ? variable_type : color(Surface::OUTLINE);
    if (style == GraphNodeStyle::VARIABLE_SETTER) {
        style_graph_node_title_icon(this, "MemberProperty", variable_type);
    } else if (!getter) {
        switch (role) {
            case GraphNodeHeader::EVENT: style_graph_node_title_icon(this, "Signal", Color::html("#ed9488")); break;
            case GraphNodeHeader::CALL: style_graph_node_title_icon(this, "MemberMethod", Color::html("#a6b1c8")); break;
            case GraphNodeHeader::VALUE: style_graph_node_title_icon(this, "MemberProperty", Color::html("#8fca9d")); break;
            case GraphNodeHeader::CONTROL: style_graph_node_title_icon(this, "Control", Color::html("#79c8ca")); break;
            case GraphNodeHeader::CUSTOM_CODE: break;
        }
    }
    outline = graph_node_outline(this, getter ? graph_compact_read_radius : graph_node_corner_radius);
    set_notify_local_transform(true);
}

void EditorGraphNode::set_slot_connected(int32_t slot, bool input, bool connected) {
    auto &slots = input ? connected_inputs : connected_outputs;
    if (connected) {
        slots.insert(slot);
    } else {
        slots.erase(slot);
    }
    queue_redraw();
}

void EditorGraphNode::set_paused(bool paused) {
    if (paused == paused_badge.is_valid()) {
        return;
    }
    paused_badge = paused ? graph_node_paused_badge(this) : Ref<StyleBoxFlat>();
    queue_redraw();
}

void register_editor_graph() {
    GDREGISTER_INTERNAL_CLASS(DisabledGraphOverlay);
    GDREGISTER_INTERNAL_CLASS(EditorGraphNode);
}

} // namespace crimson::editor_ui

#endif
