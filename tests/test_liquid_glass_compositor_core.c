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

static bool parse_blob(const uint8_t *blob, size_t size,
                       liquid_glass_indexed_image_t *image,
                       uint16_t palette[LIQUID_GLASS_PALETTE_SIZE])
{
    return liquid_glass_indexed_image_parse(blob, size, image, palette);
}

static void test_indexed_image(void)
{
    enum { WIDTH = 3, HEIGHT = 2 };
    uint8_t blob[LIQUID_GLASS_INDEXED_HEADER_BYTES +
                 LIQUID_GLASS_INDEXED_PALETTE_BYTES + WIDTH * HEIGHT] = { 0 };
    memcpy(blob, "LGP8", 4);
    blob[4] = WIDTH;
    blob[6] = HEIGHT;
    blob[8] = 3;
    const uint16_t colors[3] = { 0x0000, 0xF800, 0x07FF };
    for (int index = 0; index < 3; ++index) {
        blob[LIQUID_GLASS_INDEXED_HEADER_BYTES + index * 2] =
            (uint8_t)(colors[index] & 0xFFu);
        blob[LIQUID_GLASS_INDEXED_HEADER_BYTES + index * 2 + 1] =
            (uint8_t)(colors[index] >> 8);
    }
    const uint8_t pixels[WIDTH * HEIGHT] = { 0, 1, 2, 2, 1, 0 };
    memcpy(blob + LIQUID_GLASS_INDEXED_HEADER_BYTES +
               LIQUID_GLASS_INDEXED_PALETTE_BYTES,
           pixels, sizeof(pixels));

    uint16_t palette[LIQUID_GLASS_PALETTE_SIZE];
    memset(palette, 0xAB, sizeof(palette));
    liquid_glass_indexed_image_t image;
    assert(parse_blob(blob, sizeof(blob), &image, palette));
    assert(image.width == WIDTH && image.height == HEIGHT);
    assert(image.colors == 3);
    assert(palette[1] == 0xF800 && palette[2] == 0x07FF);
    assert(palette[3] == 0 && palette[LIQUID_GLASS_PALETTE_SIZE - 1] == 0);

    uint16_t row[WIDTH];
    liquid_glass_indexed_row(image.indices + WIDTH, palette, 0, WIDTH - 1,
                             row);
    assert(row[0] == 0x07FF && row[1] == 0xF800 && row[2] == 0x0000);
    liquid_glass_indexed_row(image.indices, palette, 1, 2, row);
    assert(row[0] == 0xF800 && row[1] == 0x07FF);

    // Every rejection leaves the caller's state untouched.
    liquid_glass_indexed_image_t untouched = image;
    assert(!parse_blob(blob, sizeof(blob) - 1, &untouched, palette));
    assert(!parse_blob(blob, sizeof(blob) + 1, &untouched, palette));
    blob[0] = 'X';
    assert(!parse_blob(blob, sizeof(blob), &untouched, palette));
    blob[0] = 'L';
    blob[8] = 0;
    assert(!parse_blob(blob, sizeof(blob), &untouched, palette));
    blob[8] = 1;
    blob[9] = 1;  // 257 colors
    assert(!parse_blob(blob, sizeof(blob), &untouched, palette));
    blob[8] = 3;
    blob[9] = 0;
    blob[4] = 0;  // zero width
    assert(!parse_blob(blob, sizeof(blob), &untouched, palette));
    blob[4] = WIDTH;
    assert(untouched.indices == image.indices);
    assert(parse_blob(blob, sizeof(blob), &image, palette));

    // Long rows exercise the unrolled path, including clipped starts.
    uint8_t long_row[37];
    uint16_t decoded[37];
    for (int x = 0; x < 37; ++x) long_row[x] = (uint8_t)(x % 3);
    liquid_glass_indexed_row(long_row, palette, 5, 36, decoded);
    for (int x = 5; x <= 36; ++x) {
        assert(decoded[x - 5] == colors[x % 3]);
    }
}

static void test_fill_mix_matches_lvgl(void)
{
    // Theme content surfaces and extremes, over every RGB565 background and
    // every canvas opacity the dashboard uses plus the LVGL thresholds.
    const uint16_t foregrounds[] = {
        liquid_glass_rgb888_to_rgb565(0x071421u),
        liquid_glass_rgb888_to_rgb565(0x050C13u),
        liquid_glass_rgb888_to_rgb565(0x0A1722u),
        0x0000, 0xFFFF, 0xF81F,
    };
    const uint8_t opacities[] = {
        0, 1, 2, 3, 64, 96, 160, 224, 252, 253, 254, 255,
    };
    for (size_t f = 0; f < sizeof(foregrounds) / sizeof(foregrounds[0]); ++f) {
        for (size_t o = 0; o < sizeof(opacities); ++o) {
            uint8_t opa = opacities[o];
            for (uint32_t background = 0; background <= UINT16_MAX;
                 ++background) {
                uint16_t expected =
                    opa <= LIQUID_GLASS_FILL_OPA_MIN ? (uint16_t)background
                    : opa >= LIQUID_GLASS_FILL_OPA_MAX ? foregrounds[f]
                    : lvgl_reference_mix(foregrounds[f],
                                         (uint16_t)background, opa);
                assert(liquid_glass_rgb565_fill_mix(
                           foregrounds[f], (uint16_t)background, opa) ==
                       expected);
            }
        }
    }

    uint16_t palette[LIQUID_GLASS_PALETTE_SIZE];
    uint16_t tinted[LIQUID_GLASS_PALETTE_SIZE];
    for (int index = 0; index < LIQUID_GLASS_PALETTE_SIZE; ++index) {
        palette[index] = (uint16_t)(index * 257u);
    }
    liquid_glass_palette_tint(palette, foregrounds[0], 96, tinted);
    for (int index = 0; index < LIQUID_GLASS_PALETTE_SIZE; ++index) {
        assert(tinted[index] == liquid_glass_rgb565_fill_mix(
                   foregrounds[0], palette[index], 96));
    }

    uint16_t span[LIQUID_GLASS_PALETTE_SIZE];
    memcpy(span, palette, sizeof(span));
    liquid_glass_fill_mix_span(span, LIQUID_GLASS_PALETTE_SIZE,
                               foregrounds[0], 96);
    assert(memcmp(span, tinted, sizeof(span)) == 0);
}

int main(void)
{
    test_indexed_image();
    test_fill_mix_matches_lvgl();

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

    // The in-place variant used after an indexed row decode matches.
    uint16_t in_place[LIQUID_GLASS_COMPOSITOR_WIDTH];
    memcpy(in_place, wallpaper + 17, sizeof(uint16_t) * 200);
    liquid_glass_apply_coverage_row(&lut, frames, 100, 17, 216, in_place);
    liquid_glass_composite_row(&lut, frames, wallpaper, 100, 17, 216, output);
    assert(memcmp(in_place, output, sizeof(uint16_t) * 200) == 0);

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
