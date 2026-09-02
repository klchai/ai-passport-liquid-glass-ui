#include "ui_glass_compositor.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdbool.h>
#include <string.h>

extern const uint8_t liquid_glass_wallpaper_rgb565_start[]
    asm("_binary_liquid_glass_wallpaper_rgb565_start");

struct ui_glass_compositor {
    lv_obj_t *target;
    liquid_glass_frame_t frames[LIQUID_GLASS_WINDOW_COUNT];
    liquid_glass_rgb565_lut_t lut;
    uint32_t tint_rgb888;
    uint16_t tint;
    uint8_t opacity[LIQUID_GLASS_WINDOW_COUNT];
    uint8_t draw_order[LIQUID_GLASS_WINDOW_COUNT];
    int16_t glint_progress[LIQUID_GLASS_WINDOW_COUNT];
    ui_glass_material_t material;
    bool lut_dirty;
};

static const char *TAG = "glass_compositor";

static void rebuild_lut(ui_glass_compositor_t *compositor);

static void build_edge_styles(
    const ui_glass_compositor_t *compositor,
    liquid_glass_edge_style_t styles[LIQUID_GLASS_WINDOW_COUNT])
{
    ui_glass_optics_t optics = ui_glass_optics_for_material(
        compositor->material);
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        const liquid_glass_frame_t *frame = &compositor->frames[card];
        uint32_t top_sample = ui_glass_background_at_y(
            (int16_t)(frame->y - 4));
        uint32_t bottom_sample = ui_glass_background_at_y(
            (int16_t)(frame->y + frame->height + 3));
        const uint32_t ring_colors[LIQUID_GLASS_EDGE_RING_COUNT] = {
            ui_glass_mix_rgb(compositor->tint_rgb888, 0xFFFFFFu, 92),
            ui_glass_mix_rgb(compositor->tint_rgb888, top_sample, 104),
            ui_glass_mix_rgb(compositor->tint_rgb888, bottom_sample, 132),
        };
        for (uint8_t ring = 0; ring < LIQUID_GLASS_EDGE_RING_COUNT;
             ++ring) {
            styles[card].ring_color[ring] =
                liquid_glass_rgb888_to_rgb565(ring_colors[ring]);
            styles[card].ring_opacity[ring] = ui_glass_scale_opacity(
                optics.ring_opacity[ring], frame->border_opacity);
        }
        styles[card].top_specular_color = liquid_glass_rgb888_to_rgb565(
            ui_glass_mix_rgb(top_sample, 0xFFFFFFu, 128));
        styles[card].bottom_refraction_color =
            liquid_glass_rgb888_to_rgb565(ui_glass_mix_rgb(
                bottom_sample, compositor->tint_rgb888, 72));
        styles[card].top_specular_opacity = ui_glass_scale_opacity(
            optics.top_specular_opacity, frame->border_opacity);
        styles[card].bottom_refraction_opacity = ui_glass_scale_opacity(
            optics.bottom_refraction_opacity, frame->border_opacity);
        styles[card].glint_color = liquid_glass_rgb888_to_rgb565(0xF7FCFFu);
        styles[card].glint_opacity = ui_glass_scale_opacity(
            optics.glint_opacity, frame->border_opacity);
        styles[card].glint_width_percent = optics.glint_width_percent;
        styles[card].glint_progress = compositor->glint_progress[card];
    }
}

static void compositor_draw_background(lv_event_t *event)
{
    ui_glass_compositor_t *compositor = lv_event_get_user_data(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    if (!compositor || !layer || !layer->draw_buf ||
        layer->color_format != LV_COLOR_FORMAT_RGB565) {
        return;
    }
    if (compositor->lut_dirty) {
        rebuild_lut(compositor);
        compositor->lut_dirty = false;
    }

    lv_area_t area = layer->_clip_area;
    if (area.x1 < 0) area.x1 = 0;
    if (area.y1 < 0) area.y1 = 0;
    if (area.x2 >= LIQUID_GLASS_COMPOSITOR_WIDTH) {
        area.x2 = LIQUID_GLASS_COMPOSITOR_WIDTH - 1;
    }
    if (area.y2 >= LIQUID_GLASS_COMPOSITOR_HEIGHT) {
        area.y2 = LIQUID_GLASS_COMPOSITOR_HEIGHT - 1;
    }
    if (area.x1 > area.x2 || area.y1 > area.y2) return;

    const uint16_t *wallpaper =
        (const uint16_t *)liquid_glass_wallpaper_rgb565_start;
    liquid_glass_edge_style_t edge_styles[LIQUID_GLASS_WINDOW_COUNT] = { 0 };
    build_edge_styles(compositor, edge_styles);
    for (int16_t y = area.y1; y <= area.y2; ++y) {
        uint16_t *output = lv_draw_buf_goto_xy(
            layer->draw_buf,
            (uint32_t)(area.x1 - layer->buf_area.x1),
            (uint32_t)(y - layer->buf_area.y1));
        if (!output) return;
        liquid_glass_composite_row(
            &compositor->lut,
            compositor->frames,
            wallpaper + (size_t)y * LIQUID_GLASS_COMPOSITOR_WIDTH,
            y, area.x1, area.x2, output);
        liquid_glass_composite_edges_row(
            compositor->frames, compositor->draw_order, edge_styles,
            y, area.x1, area.x2, output);
    }
}

static void compositor_cover_check(lv_event_t *event)
{
    // The custom draw callback writes every pixel in the object's clipped
    // area, even though the base object's style is transparent.
    lv_event_set_cover_res(event, LV_COVER_RES_COVER);
}

static void rebuild_lut(ui_glass_compositor_t *compositor)
{
    liquid_glass_rgb565_lut_build(&compositor->lut,
                                  compositor->tint,
                                  compositor->opacity);
}

static void invalidate_deck_bounds(ui_glass_compositor_t *compositor)
{
    bool found = false;
    lv_area_t area = { 0 };
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        const liquid_glass_frame_t *frame = &compositor->frames[card];
        if (frame->width <= 0 || frame->height <= 0) continue;
        lv_area_t card_area = {
            .x1 = frame->x,
            .y1 = frame->y,
            .x2 = frame->x + frame->width - 1,
            .y2 = frame->y + frame->height - 1,
        };
        if (!found) {
            area = card_area;
            found = true;
        } else {
            if (card_area.x1 < area.x1) area.x1 = card_area.x1;
            if (card_area.y1 < area.y1) area.y1 = card_area.y1;
            if (card_area.x2 > area.x2) area.x2 = card_area.x2;
            if (card_area.y2 > area.y2) area.y2 = card_area.y2;
        }
    }
    if (found) lv_obj_invalidate_area(compositor->target, &area);
}

static void compositor_deleted(lv_event_t *event)
{
    ui_glass_compositor_t *compositor = lv_event_get_user_data(event);
    heap_caps_free(compositor);
}

ui_glass_compositor_t *ui_glass_compositor_create(lv_obj_t *parent,
                                                   uint32_t tint,
                                                   ui_glass_material_t material)
{
    if (!parent) return NULL;

    // Pixel/LUT state is renderer memory, not LVGL object metadata. Keeping it
    // out of the deliberately small 24 KiB LVGL pool leaves room for transient
    // glyph and draw-task caches during animation.
    ui_glass_compositor_t *compositor = heap_caps_calloc(
        1, sizeof(ui_glass_compositor_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!compositor) return NULL;
    compositor->tint_rgb888 = tint;
    compositor->tint = liquid_glass_rgb888_to_rgb565(tint);
    compositor->material = material;
    compositor->draw_order[0] = 2;
    compositor->draw_order[1] = 1;
    compositor->draw_order[2] = 0;
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        compositor->glint_progress[card] = INT16_MIN;
    }
    compositor->lut_dirty = true;
    compositor->target = lv_obj_create(parent);
    if (!compositor->target) {
        heap_caps_free(compositor);
        return NULL;
    }
    lv_obj_remove_flag(compositor->target, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(compositor->target, 0, 0);
    lv_obj_set_size(compositor->target,
                    LIQUID_GLASS_COMPOSITOR_WIDTH,
                    LIQUID_GLASS_COMPOSITOR_HEIGHT);
    lv_obj_set_style_pad_all(compositor->target, 0, 0);
    lv_obj_set_style_border_width(compositor->target, 0, 0);
    lv_obj_set_style_bg_opa(compositor->target, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(compositor->target, compositor_cover_check,
                        LV_EVENT_COVER_CHECK, compositor);
    lv_obj_add_event_cb(compositor->target, compositor_draw_background,
                        LV_EVENT_DRAW_MAIN, compositor);
    lv_obj_add_event_cb(compositor->target, compositor_deleted,
                        LV_EVENT_DELETE, compositor);
    ESP_LOGI(TAG, "direct background state=%u bytes, heap free/largest=%u/%u",
             (unsigned)sizeof(*compositor),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return compositor;
}

void ui_glass_compositor_set_frame(ui_glass_compositor_t *compositor,
                                   uint8_t card_index,
                                   liquid_glass_frame_t frame)
{
    if (!compositor || card_index >= LIQUID_GLASS_WINDOW_COUNT) return;
    bool opacity_changed =
        compositor->opacity[card_index] != frame.surface_opacity;
    compositor->frames[card_index] = frame;
    compositor->opacity[card_index] = frame.surface_opacity;
    if (opacity_changed) compositor->lut_dirty = true;
}

void ui_glass_compositor_set_edge_strength(
    ui_glass_compositor_t *compositor,
    uint8_t card_index,
    uint8_t strength)
{
    if (!compositor || card_index >= LIQUID_GLASS_WINDOW_COUNT) return;
    compositor->frames[card_index].border_opacity = strength;
}

void ui_glass_compositor_set_glint(ui_glass_compositor_t *compositor,
                                   uint8_t card_index,
                                   int16_t progress)
{
    if (!compositor || card_index >= LIQUID_GLASS_WINDOW_COUNT) return;
    compositor->glint_progress[card_index] = progress;
}

void ui_glass_compositor_set_draw_order(
    ui_glass_compositor_t *compositor,
    const uint8_t draw_order[LIQUID_GLASS_WINDOW_COUNT])
{
    if (!compositor || !draw_order) return;
    memcpy(compositor->draw_order, draw_order, sizeof(compositor->draw_order));
}

void ui_glass_compositor_commit_frames(
    ui_glass_compositor_t *compositor,
    const liquid_glass_frame_t frames[LIQUID_GLASS_WINDOW_COUNT],
    const liquid_glass_dirty_rect_t *visual_regions,
    size_t visual_region_count)
{
    if (!compositor || !frames ||
        (!visual_regions && visual_region_count > 0)) {
        return;
    }

    liquid_glass_dirty_rect_t dirty[LIQUID_GLASS_MAX_DIRTY_RECTS];
    size_t dirty_count = liquid_glass_plan_visual_dirty_rects(
        compositor->frames, frames, visual_regions, visual_region_count,
        dirty);
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        if (compositor->opacity[card] != frames[card].surface_opacity) {
            compositor->lut_dirty = true;
        }
        compositor->opacity[card] = frames[card].surface_opacity;
    }
    memcpy(compositor->frames, frames, sizeof(compositor->frames));

    for (size_t index = 0; index < dirty_count; ++index) {
        lv_area_t area = {
            .x1 = dirty[index].x1,
            .y1 = dirty[index].y1,
            .x2 = dirty[index].x2,
            .y2 = dirty[index].y2,
        };
        lv_obj_invalidate_area(compositor->target, &area);
    }
}

void ui_glass_compositor_invalidate_visual_regions(
    ui_glass_compositor_t *compositor,
    const liquid_glass_dirty_rect_t *visual_regions,
    size_t visual_region_count)
{
    if (!compositor || !visual_regions || visual_region_count == 0) return;

    liquid_glass_dirty_rect_t dirty[LIQUID_GLASS_MAX_DIRTY_RECTS];
    size_t dirty_count = liquid_glass_plan_visual_dirty_rects(
        compositor->frames, compositor->frames,
        visual_regions, visual_region_count, dirty);
    for (size_t index = 0; index < dirty_count; ++index) {
        lv_area_t area = {
            .x1 = dirty[index].x1,
            .y1 = dirty[index].y1,
            .x2 = dirty[index].x2,
            .y2 = dirty[index].y2,
        };
        lv_obj_invalidate_area(compositor->target, &area);
    }
}

void ui_glass_compositor_set_material(
    ui_glass_compositor_t *compositor,
    uint32_t tint,
    ui_glass_material_t material,
    const uint8_t opacity[LIQUID_GLASS_WINDOW_COUNT])
{
    if (!compositor || !opacity) return;
    compositor->tint_rgb888 = tint;
    compositor->tint = liquid_glass_rgb888_to_rgb565(tint);
    compositor->material = material;
    memcpy(compositor->opacity, opacity, sizeof(compositor->opacity));
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        compositor->frames[card].surface_opacity = opacity[card];
    }
    compositor->lut_dirty = true;
    invalidate_deck_bounds(compositor);
}
