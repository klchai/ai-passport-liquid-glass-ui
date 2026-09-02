#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "liquid_glass_compositor_core.h"

static int16_t integer_sqrt(int32_t value)
{
    int16_t result = 0;
    while ((int32_t)(result + 1) * (result + 1) <= value) ++result;
    return result;
}

static bool naive_card_contains(const liquid_glass_frame_t *frame,
                                int16_t x, int16_t y)
{
    if (x < frame->x || x >= frame->x + frame->width ||
        y < frame->y || y >= frame->y + frame->height) {
        return false;
    }
    int16_t local_y = (int16_t)(y - frame->y);
    int16_t radius = LIQUID_GLASS_CARD_RADIUS;
    if (radius * 2 > frame->height) radius = frame->height / 2;
    int16_t edge_y;
    if (local_y < radius) {
        edge_y = (int16_t)(radius - 1 - local_y);
    } else if (local_y >= frame->height - radius) {
        edge_y = (int16_t)(local_y - (frame->height - radius));
    } else {
        return true;
    }
    int16_t inset = (int16_t)(radius - integer_sqrt(
        (int32_t)radius * radius - (int32_t)edge_y * edge_y));
    return x >= frame->x + inset &&
           x <= frame->x + frame->width - 1 - inset;
}

static uint8_t naive_mask(
    const liquid_glass_frame_t frames[LIQUID_GLASS_WINDOW_COUNT],
    int16_t x, int16_t y)
{
    uint8_t mask = 0;
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        if (naive_card_contains(&frames[card], x, y)) {
            mask |= (uint8_t)(1u << card);
        }
    }
    return mask;
}

static uint16_t lvgl_reference_mix(uint16_t foreground, uint16_t background,
                                   uint8_t opacity)
{
    if (opacity == 255) return foreground;
    if (opacity == 0) return background;
    if (foreground == background) return foreground;
    uint32_t mix = ((uint32_t)opacity + 4u) >> 3;
    uint32_t bg = (uint32_t)(background | ((uint32_t)background << 16)) &
                  0x7E0F81Fu;
    uint32_t fg = (uint32_t)(foreground | ((uint32_t)foreground << 16)) &
                  0x7E0F81Fu;
    uint32_t result = ((((fg - bg) * mix) >> 5) + bg) & 0x7E0F81Fu;
    return (uint16_t)((result >> 16) | result);
}

static bool rect_contains(const liquid_glass_dirty_rect_t *rect,
                          int16_t x, int16_t y)
{
    return x >= rect->x1 && x <= rect->x2 &&
           y >= rect->y1 && y <= rect->y2;
}

int main(void)
{
    liquid_glass_frame_t frames[LIQUID_GLASS_WINDOW_COUNT];
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        frames[card] = liquid_glass_frame_for_rank(card);
    }

    for (int16_t y = 0; y < LIQUID_GLASS_COMPOSITOR_HEIGHT; ++y) {
        liquid_glass_span_t spans[LIQUID_GLASS_MAX_ROW_SPANS];
        size_t count = liquid_glass_coverage_spans(
            frames, y, 0, LIQUID_GLASS_COMPOSITOR_WIDTH - 1, spans);
        for (int16_t x = 0; x < LIQUID_GLASS_COMPOSITOR_WIDTH; ++x) {
            uint8_t actual = 0;
            for (size_t span = 0; span < count; ++span) {
                if (x >= spans[span].x1 && x <= spans[span].x2) {
                    assert(actual == 0);
                    actual = spans[span].coverage_mask;
                }
            }
            assert(actual == naive_mask(frames, x, y));
        }
    }

    const uint8_t opacity[LIQUID_GLASS_WINDOW_COUNT] = { 52, 67, 84 };
    const uint16_t tint = liquid_glass_rgb888_to_rgb565(0x526677u);
    liquid_glass_rgb565_lut_t lut;
    liquid_glass_rgb565_lut_build(&lut, tint, opacity);
    for (uint32_t background = 0; background <= UINT16_MAX; ++background) {
        for (uint8_t mask = 0; mask < LIQUID_GLASS_COVERAGE_STATES; ++mask) {
            uint16_t expected = (uint16_t)background;
            for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
                if (mask & (uint8_t)(1u << card)) {
                    expected = lvgl_reference_mix(
                        tint, expected, opacity[card]);
                }
            }
            assert(liquid_glass_rgb565_lut_apply(
                &lut, (uint16_t)background, mask) == expected);
        }
    }

    uint16_t wallpaper[LIQUID_GLASS_COMPOSITOR_WIDTH];
    uint16_t output[LIQUID_GLASS_COMPOSITOR_WIDTH];
    for (int x = 0; x < LIQUID_GLASS_COMPOSITOR_WIDTH; ++x) {
        wallpaper[x] = (uint16_t)(x * 251u);
    }
    liquid_glass_composite_row(&lut, frames, wallpaper, 100, 0,
                               LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    for (int16_t x = 0; x < LIQUID_GLASS_COMPOSITOR_WIDTH; ++x) {
        assert(output[x] == liquid_glass_rgb565_lut_apply(
            &lut, wallpaper[x], naive_mask(frames, x, 100)));
    }

    liquid_glass_frame_t edge_frames[LIQUID_GLASS_WINDOW_COUNT] = { 0 };
    edge_frames[0] = (liquid_glass_frame_t) {
        .x = 20, .y = 40, .width = 100, .height = 80,
    };
    const uint8_t no_fill[LIQUID_GLASS_WINDOW_COUNT] = { 0, 0, 0 };
    liquid_glass_rgb565_lut_t edge_lut;
    liquid_glass_rgb565_lut_build(&edge_lut, 0, no_fill);
    liquid_glass_edge_style_t edge_styles[LIQUID_GLASS_WINDOW_COUNT] = { 0 };
    edge_styles[0].ring_color[0] = 0xF800;
    edge_styles[0].ring_opacity[0] = 255;
    const uint8_t draw_order[LIQUID_GLASS_WINDOW_COUNT] = { 2, 1, 0 };
    liquid_glass_composite_row(&edge_lut, edge_frames, wallpaper, 40, 0,
                               LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    liquid_glass_composite_edges_row(
        edge_frames, draw_order, edge_styles, 40, 0,
        LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    assert(output[50] == 0xF800);
    assert(output[10] == wallpaper[10]);

    liquid_glass_composite_row(&edge_lut, edge_frames, wallpaper, 50, 0,
                               LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    liquid_glass_composite_edges_row(
        edge_frames, draw_order, edge_styles, 50, 0,
        LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    assert(output[50] == wallpaper[50]);

    edge_styles[0].glint_color = 0x07E0;
    edge_styles[0].glint_opacity = 255;
    edge_styles[0].glint_width_percent = 50;
    edge_styles[0].glint_progress = 512;
    liquid_glass_composite_row(&edge_lut, edge_frames, wallpaper, 42, 0,
                               LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    liquid_glass_composite_edges_row(
        edge_frames, draw_order, edge_styles, 42, 0,
        LIQUID_GLASS_COMPOSITOR_WIDTH - 1, output);
    assert(output[69] == 0x07E0);

    liquid_glass_frame_t next[LIQUID_GLASS_WINDOW_COUNT];
    memcpy(next, frames, sizeof(next));
    for (uint8_t card = 0; card < LIQUID_GLASS_WINDOW_COUNT; ++card) {
        liquid_glass_frame_t target = liquid_glass_frame_for_rank(
            (uint8_t)((card + 1) % LIQUID_GLASS_WINDOW_COUNT));
        next[card] = liquid_glass_transition_frame(
            frames[card], target, card == 0,
            LIQUID_GLASS_MOTION_PROGRESS_MAX / 16);
    }

    liquid_glass_dirty_rect_t dirty[LIQUID_GLASS_MAX_DIRTY_RECTS];
    size_t dirty_count = liquid_glass_plan_dirty_rects(frames, next, dirty);
    assert(dirty_count > 0);
    assert(dirty_count <= LIQUID_GLASS_MAX_DIRTY_RECTS);
    uint32_t dirty_area = 0;
    for (size_t index = 0; index < dirty_count; ++index) {
        dirty_area += (uint32_t)(dirty[index].x2 - dirty[index].x1 + 1) *
                      (uint32_t)(dirty[index].y2 - dirty[index].y1 + 1);
    }
    assert(dirty_area < 220u * 220u);
    for (int16_t y = 0; y < LIQUID_GLASS_COMPOSITOR_HEIGHT; ++y) {
        for (int16_t x = 0; x < LIQUID_GLASS_COMPOSITOR_WIDTH; ++x) {
            if (naive_mask(frames, x, y) == naive_mask(next, x, y)) continue;
            bool covered = false;
            for (size_t index = 0; index < dirty_count; ++index) {
                covered |= rect_contains(&dirty[index], x, y);
            }
            assert(covered);
        }
    }

    assert(liquid_glass_plan_dirty_rects(frames, frames, dirty) == 0);

    const liquid_glass_dirty_rect_t visual_regions[] = {
        { .x1 = -4, .y1 = 9, .x2 = 13, .y2 = 18 },
        { .x1 = 217, .y1 = 301, .x2 = 246, .y2 = 328 },
        { .x1 = 96, .y1 = 150, .x2 = 143, .y2 = 169 },
    };
    dirty_count = liquid_glass_plan_visual_dirty_rects(
        frames, frames, visual_regions,
        sizeof(visual_regions) / sizeof(visual_regions[0]), dirty);
    assert(dirty_count > 0);
    assert(dirty_count <= LIQUID_GLASS_MAX_DIRTY_RECTS);
    for (size_t region = 0;
         region < sizeof(visual_regions) / sizeof(visual_regions[0]);
         ++region) {
        int16_t x1 = visual_regions[region].x1 < 0
            ? 0 : visual_regions[region].x1;
        int16_t y1 = visual_regions[region].y1 < 0
            ? 0 : visual_regions[region].y1;
        int16_t x2 = visual_regions[region].x2 >=
                     LIQUID_GLASS_COMPOSITOR_WIDTH
            ? LIQUID_GLASS_COMPOSITOR_WIDTH - 1
            : visual_regions[region].x2;
        int16_t y2 = visual_regions[region].y2 >=
                     LIQUID_GLASS_COMPOSITOR_HEIGHT
            ? LIQUID_GLASS_COMPOSITOR_HEIGHT - 1
            : visual_regions[region].y2;
        for (int16_t y = y1; y <= y2; ++y) {
            for (int16_t x = x1; x <= x2; ++x) {
                bool covered = false;
                for (size_t index = 0; index < dirty_count; ++index) {
                    covered |= rect_contains(&dirty[index], x, y);
                }
                assert(covered);
            }
        }
    }
    return 0;
}
