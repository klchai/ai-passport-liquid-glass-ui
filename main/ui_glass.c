#include "ui_glass.h"

typedef struct {
    int16_t radius;
    int16_t glint_progress;
    uint8_t edge_strength;
    bool fused_edge;
    uint32_t tint;
    ui_glass_material_t material;
} ui_glass_surface_state_t;

static void invalidate_area(lv_obj_t *surface, int16_t x1, int16_t y1,
                            int16_t x2, int16_t y2)
{
    if (x1 > x2 || y1 > y2) return;
    lv_area_t area = { .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2 };
    lv_obj_invalidate_area(surface, &area);
}

static void invalidate_perimeter(lv_obj_t *surface)
{
    lv_area_t bounds;
    lv_obj_get_coords(surface, &bounds);
    const int16_t thickness = UI_GLASS_OPTIC_RING_COUNT + 2;
    invalidate_area(surface, bounds.x1, bounds.y1, bounds.x2,
                    (int16_t)(bounds.y1 + thickness - 1));
    invalidate_area(surface, bounds.x1,
                    (int16_t)(bounds.y2 - thickness + 1),
                    bounds.x2, bounds.y2);
    invalidate_area(surface, bounds.x1,
                    (int16_t)(bounds.y1 + thickness),
                    (int16_t)(bounds.x1 + thickness - 1),
                    (int16_t)(bounds.y2 - thickness));
    invalidate_area(surface,
                    (int16_t)(bounds.x2 - thickness + 1),
                    (int16_t)(bounds.y1 + thickness),
                    bounds.x2, (int16_t)(bounds.y2 - thickness));
}

static bool glint_area(const lv_obj_t *surface,
                       const ui_glass_surface_state_t *state,
                       int16_t progress,
                       lv_area_t *area)
{
    ui_glass_optics_t optics = ui_glass_optics_for_material(state->material);
    if (progress <= -256 || progress >= 1280 ||
        optics.glint_width_percent == 0) {
        return false;
    }

    lv_area_t bounds;
    lv_obj_get_coords(surface, &bounds);
    int16_t radius = state->radius;
    int16_t width = (int16_t)lv_area_get_width(&bounds);
    if (radius > width / 2) radius = width / 2;
    int16_t safe_left = (int16_t)(bounds.x1 + radius);
    int16_t safe_right = (int16_t)(bounds.x2 - radius);
    int16_t safe_width = (int16_t)(safe_right - safe_left);
    if (safe_width <= 0) return false;

    int16_t center = ui_glass_glint_center(progress, safe_left, safe_right);
    int16_t half = (int16_t)(safe_width * optics.glint_width_percent / 200);
    area->x1 = center - half - 2;
    area->x2 = center + half + 2;
    if (area->x1 < safe_left - 2) area->x1 = safe_left - 2;
    if (area->x2 > safe_right + 2) area->x2 = safe_right + 2;
    area->y1 = bounds.y1;
    area->y2 = (int16_t)(bounds.y1 + 4);
    return true;
}

static lv_obj_t *plain_object(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    return object;
}

static void draw_segment(lv_layer_t *layer, int16_t x1, int16_t x2, int16_t y,
                         uint32_t color, uint8_t opacity, int16_t width)
{
    if (x2 <= x1 || opacity == 0) return;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.p1.x = x1;
    line.p1.y = y;
    line.p2.x = x2;
    line.p2.y = y;
    line.color = lv_color_hex(color);
    line.opa = opacity;
    line.width = width;
    line.round_start = true;
    line.round_end = true;
    lv_draw_line(layer, &line);
}

static void surface_draw(lv_event_t *event)
{
    lv_obj_t *surface = lv_event_get_target_obj(event);
    ui_glass_surface_state_t *state = lv_event_get_user_data(event);
    if (!state) return;

    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(surface, &bounds);
    lv_area_increase(&bounds,
                     lv_obj_get_style_transform_width(surface, LV_PART_MAIN),
                     lv_obj_get_style_transform_height(surface, LV_PART_MAIN));
    int16_t width = (int16_t)lv_area_get_width(&bounds);
    int16_t radius = state->radius;
    if (radius > width / 2) radius = width / 2;

    ui_glass_optics_t optics = ui_glass_optics_for_material(state->material);
    uint8_t strength = state->edge_strength;
    if (strength == 0) return;
    if (state->fused_edge) return;
    int16_t safe_left = (int16_t)(bounds.x1 + radius);
    int16_t safe_right = (int16_t)(bounds.x2 - radius);
    int16_t safe_width = safe_right - safe_left;
    if (safe_width <= 0) return;

    {
        uint32_t top_sample = ui_glass_background_at_y(
            (int16_t)(bounds.y1 - 4));
        uint32_t bottom_sample = ui_glass_background_at_y(
            (int16_t)(bounds.y2 + 4));
        const uint32_t ring_colors[UI_GLASS_OPTIC_RING_COUNT] = {
            ui_glass_mix_rgb(state->tint, 0xFFFFFFu, 92),
            ui_glass_mix_rgb(state->tint, top_sample, 104),
            ui_glass_mix_rgb(state->tint, bottom_sample, 132),
        };

        for (int ring = 0; ring < UI_GLASS_OPTIC_RING_COUNT; ++ring) {
            lv_area_t ring_bounds = bounds;
            lv_area_increase(&ring_bounds, -ring, -ring);

            lv_draw_border_dsc_t border;
            lv_draw_border_dsc_init(&border);
            border.color = lv_color_hex(ring_colors[ring]);
            border.width = 1;
            border.opa = ui_glass_scale_opacity(
                optics.ring_opacity[ring], strength);
            border.radius = radius > ring ? radius - ring : 0;
            border.side = LV_BORDER_SIDE_FULL;
            lv_draw_border(layer, &border, &ring_bounds);
        }

        // Highlights occupy different parts of the perimeter, avoiding the
        // doubled full-width white rules that made the earlier version look
        // artificial.
        draw_segment(layer, safe_left,
                     (int16_t)(safe_left + safe_width * 30 / 100),
                     (int16_t)(bounds.y1 + 1),
                     ui_glass_mix_rgb(top_sample, 0xFFFFFFu, 128),
                     ui_glass_scale_opacity(
                         optics.top_specular_opacity, strength), 1);
        draw_segment(layer,
                     (int16_t)(safe_right - safe_width * 36 / 100),
                     safe_right, (int16_t)(bounds.y2 - 1),
                     ui_glass_mix_rgb(bottom_sample, state->tint, 72),
                     ui_glass_scale_opacity(
                         optics.bottom_refraction_opacity, strength), 1);
    }

    if (state->glint_progress <= -256 || state->glint_progress >= 1280 ||
        optics.glint_width_percent == 0) {
        return;
    }

    int16_t center = ui_glass_glint_center(state->glint_progress,
                                           safe_left, safe_right);
    int16_t half = (int16_t)(safe_width * optics.glint_width_percent / 200);
    int16_t glint_left = center - half;
    int16_t glint_right = center + half;
    if (glint_left < safe_left) glint_left = safe_left;
    if (glint_right > safe_right) glint_right = safe_right;
    draw_segment(layer, glint_left, glint_right, (int16_t)(bounds.y1 + 2),
                 0xF7FCFFu,
                 ui_glass_scale_opacity(optics.glint_opacity, strength), 2);
}

lv_obj_t *ui_glass_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x02060B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);

    // The compositor's first child covers the screen and fills the active LVGL
    // draw buffer directly, so the base screen must not draw behind it.
    return screen;
}

lv_obj_t *ui_glass_surface_create(lv_obj_t *parent, int x, int y, int w, int h,
                                  int radius, uint32_t tint, lv_opa_t opacity,
                                  ui_glass_material_t material)
{
    lv_obj_t *surface = plain_object(parent, x, y, w, h);
    lv_obj_set_style_radius(surface, radius, 0);
    lv_obj_set_style_bg_color(surface, lv_color_hex(tint), 0);
    lv_obj_set_style_bg_opa(surface, opacity, 0);

    // The standard border is disabled; edge strength lives in the custom
    // renderer so an edge-only animation does not invalidate the card center.
    ui_glass_optics_t optics = ui_glass_optics_for_material(material);
    lv_obj_set_style_border_width(surface, 0, 0);

    ui_glass_surface_state_t *state =
        lv_malloc_zeroed(sizeof(ui_glass_surface_state_t));
    if (state) {
        state->radius = (int16_t)radius;
        state->glint_progress = UI_GLASS_GLINT_HIDDEN;
        state->edge_strength = optics.edge_strength;
        state->tint = tint;
        state->material = material;
        lv_obj_set_user_data(surface, state);
        lv_obj_add_event_cb(surface, surface_draw, LV_EVENT_DRAW_MAIN_END, state);
        lv_obj_add_event_cb(surface, lv_event_free_user_data_cb,
                            LV_EVENT_DELETE, state);
    }
    return surface;
}

void ui_glass_surface_set_material(lv_obj_t *surface,
                                   ui_glass_material_t material)
{
    ui_glass_surface_state_t *state = lv_obj_get_user_data(surface);
    if (!state || state->material == material) return;
    state->material = material;
    invalidate_perimeter(surface);
}

void ui_glass_surface_set_tint(lv_obj_t *surface, uint32_t tint,
                               lv_opa_t opacity)
{
    if (!surface) return;
    ui_glass_surface_state_t *state = lv_obj_get_user_data(surface);
    if (state) state->tint = tint;
    lv_obj_set_style_bg_color(surface, lv_color_hex(tint), 0);
    lv_obj_set_style_bg_opa(surface, opacity, 0);
    lv_obj_invalidate(surface);
}

void ui_glass_surface_set_edge_strength(lv_obj_t *surface, uint8_t strength)
{
    ui_glass_surface_state_t *state = lv_obj_get_user_data(surface);
    if (!state || state->edge_strength == strength) return;
    state->edge_strength = strength;
    invalidate_perimeter(surface);
}

uint8_t ui_glass_surface_get_edge_strength(lv_obj_t *surface)
{
    const ui_glass_surface_state_t *state = lv_obj_get_user_data(surface);
    return state ? state->edge_strength : 0;
}

void ui_glass_surface_set_fused_edge(lv_obj_t *surface, bool fused)
{
    ui_glass_surface_state_t *state = lv_obj_get_user_data(surface);
    if (!state || state->fused_edge == fused) return;
    state->fused_edge = fused;
    invalidate_perimeter(surface);
}

void ui_glass_surface_set_glint(lv_obj_t *surface, int32_t progress)
{
    ui_glass_surface_state_t *state = lv_obj_get_user_data(surface);
    if (!state || state->glint_progress == progress) return;
    lv_area_t old_area;
    lv_area_t new_area;
    bool old_visible = glint_area(surface, state, state->glint_progress,
                                  &old_area);
    bool new_visible = glint_area(surface, state, (int16_t)progress,
                                  &new_area);
    state->glint_progress = (int16_t)progress;
    if (old_visible && new_visible) {
        if (new_area.x1 < old_area.x1) old_area.x1 = new_area.x1;
        if (new_area.y1 < old_area.y1) old_area.y1 = new_area.y1;
        if (new_area.x2 > old_area.x2) old_area.x2 = new_area.x2;
        if (new_area.y2 > old_area.y2) old_area.y2 = new_area.y2;
        lv_obj_invalidate_area(surface, &old_area);
    } else if (old_visible) {
        lv_obj_invalidate_area(surface, &old_area);
    } else if (new_visible) {
        lv_obj_invalidate_area(surface, &new_area);
    }
}

lv_obj_t *ui_glass_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}
