#pragma once

#include "liquid_glass_compositor_core.h"
#include "ui_glass_optics.h"
#include "lvgl.h"

#include <stdint.h>

typedef struct ui_glass_compositor ui_glass_compositor_t;

// Creates one covering RGB565 renderer that fuses the wallpaper and
// overlapping deck fills straight into LVGL's active draw buffer. Card
// content remains as ordinary objects above it; deck fills and static optical
// edges share the same direct RGB565 pass.
ui_glass_compositor_t *ui_glass_compositor_create(lv_obj_t *parent,
                                                   uint32_t tint,
                                                   ui_glass_material_t material);

// Updates one geometry/material input. Materialization uses this path while
// ordinary LVGL object invalidation owns the transition redraw.
void ui_glass_compositor_set_frame(ui_glass_compositor_t *compositor,
                                   uint8_t card_index,
                                   liquid_glass_frame_t frame);

// Updates one fused edge channel without scheduling a second invalidation.
// The owning surface animation already invalidates the exact old/new rim.
void ui_glass_compositor_set_edge_strength(
    ui_glass_compositor_t *compositor,
    uint8_t card_index,
    uint8_t strength);

// Updates the fused two-pixel specular sweep. The owning surface computes and
// invalidates the exact old/new footprint.
void ui_glass_compositor_set_glint(ui_glass_compositor_t *compositor,
                                   uint8_t card_index,
                                   int16_t progress);

// Physical back-to-front order is independent of stable card identity.
void ui_glass_compositor_set_draw_order(
    ui_glass_compositor_t *compositor,
    const uint8_t draw_order[LIQUID_GLASS_WINDOW_COUNT]);

// Atomically commits all card frames and invalidates one coalesced tile plan.
// visual_regions are inclusive screen-space bounds for moving content and
// optical edges whose pixels can change even when fill coverage is unchanged.
void ui_glass_compositor_commit_frames(
    ui_glass_compositor_t *compositor,
    const liquid_glass_frame_t frames[LIQUID_GLASS_WINDOW_COUNT],
    const liquid_glass_dirty_rect_t *visual_regions,
    size_t visual_region_count);

// Invalidates ordinary LVGL pixels through the same bounded tile planner
// without changing deck geometry. This is used for transparent containers
// whose visible children occupy far less area than the container itself.
void ui_glass_compositor_invalidate_visual_regions(
    ui_glass_compositor_t *compositor,
    const liquid_glass_dirty_rect_t *visual_regions,
    size_t visual_region_count);

void ui_glass_compositor_set_material(
    ui_glass_compositor_t *compositor,
    uint32_t tint,
    ui_glass_material_t material,
    const uint8_t opacity[LIQUID_GLASS_WINDOW_COUNT]);
