#include "demo.h"
#include "liquid_glass_motion.h"
#include "ui_glass.h"
#include "ui_glass_compositor.h"
#include "lvgl.h"

#include <string.h>

#define WINDOW_BAR_COUNT  6
#define DECK_MATERIAL_COUNT 3

#define DECK_CYCLE_DURATION_MS 640
#define DECK_MATERIALIZE_FRONT_MS 560
#define DECK_MATERIALIZE_BACK_MS  520
#define DECK_REFRESH_PERIOD_MS     10
#define DECK_MAX_VISUAL_REGIONS    128
#define WINDOW_MAX_CONTENT_REGIONS 16

typedef struct {
    const char *title;
    const char *number;
    const char *value;
    const char *detail;
    const char *footer;
    uint8_t bars[WINDOW_BAR_COUNT];
} window_content_t;

typedef struct {
    const char *label;
    ui_glass_material_t material;
} deck_material_t;

typedef struct {
    lv_obj_t *surface;
    lv_obj_t *content;
    lv_obj_t *dot;
    lv_obj_t *title;
    lv_obj_t *value;
    lv_obj_t *detail;
    lv_obj_t *footer;
    lv_obj_t *bars[WINDOW_BAR_COUNT];
} window_view_t;

typedef struct {
    lv_obj_t *object;
    uint8_t window_index;
    liquid_glass_frame_t start;
    liquid_glass_frame_t target;
    bool wraps_depth;
    bool switches_depth_order;
    bool depth_order_switched;
} window_motion_t;

typedef struct {
    bool depth_order_switched;
} deck_motion_t;

typedef struct {
    lv_obj_t *object;
    int16_t center_x;
} indicator_motion_t;

typedef struct {
    lv_obj_t *object;
    int16_t center_y;
    int16_t resting_height;
} bar_motion_t;

static const window_content_t WINDOW_CONTENT[LIQUID_GLASS_WINDOW_COUNT] = {
    {
        .title = "Focus", .number = "01", .value = "48 min",
        .detail = "Deep work space", .footer = "Session active",
        .bars = { 7, 13, 24, 17, 10, 20 },
    },
    {
        .title = "Voice", .number = "02", .value = "Ready",
        .detail = "Live waveform", .footer = "Mic ready",
        .bars = { 12, 22, 9, 26, 16, 7 },
    },
    {
        .title = "Link", .number = "03", .value = "2 nearby",
        .detail = "Connections visible", .footer = "Discovery on",
        .bars = { 8, 8, 14, 14, 22, 22 },
    },
};

static const deck_material_t DECK_MATERIALS[DECK_MATERIAL_COUNT] = {
    { .label = "Regular glass  1/3", .material = UI_GLASS_MATERIAL_REGULAR },
    { .label = "Clear glass  2/3", .material = UI_GLASS_MATERIAL_CLEAR },
    { .label = "Contrast glass  3/3", .material = UI_GLASS_MATERIAL_CONTRAST },
};

// Light exposure changes with occlusion, while the material's fill
// transmittance remains identical on every card in the stack.
static const uint8_t DECK_EDGE_EXPOSURE[LIQUID_GLASS_WINDOW_COUNT] = {
    255, 178, 115,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_battery;
static lv_obj_t *s_material_label;
static ui_glass_compositor_t *s_compositor;
static window_view_t s_windows[LIQUID_GLASS_WINDOW_COUNT];
static window_motion_t s_window_motion[LIQUID_GLASS_WINDOW_COUNT];
static deck_motion_t s_deck_motion;
static indicator_motion_t s_indicator_motion[LIQUID_GLASS_WINDOW_COUNT];
static bar_motion_t s_bar_motion[LIQUID_GLASS_WINDOW_COUNT][WINDOW_BAR_COUNT];
static lv_timer_t *s_showcase_timer;
static lv_timer_t *s_refresh_timer;
static uint8_t s_showcase_stage;
static int s_selected;
static int s_material_index;
static int s_battery_soc = -1;
static bool s_transitioning;

static void refresh_indicators(bool animate);
static void order_windows(void);

void demo_liquid_glass_set_battery(int soc)
{
    s_battery_soc = (soc >= 0 && soc <= 100) ? soc : -1;
}

static deck_material_t current_material(void)
{
    return DECK_MATERIALS[s_material_index];
}

static ui_glass_optics_t current_optics(void)
{
    return ui_glass_optics_for_material(current_material().material);
}

static uint8_t edge_opacity_for_rank(ui_glass_optics_t optics, uint8_t rank)
{
    if (rank >= LIQUID_GLASS_WINDOW_COUNT) {
        rank = LIQUID_GLASS_WINDOW_COUNT - 1;
    }
    return (uint8_t)(((uint16_t)optics.edge_strength *
                      DECK_EDGE_EXPOSURE[rank] + 127) / 255);
}

static lv_obj_t *flat_object(lv_obj_t *parent, int x, int y, int w, int h,
                             uint32_t color, lv_opa_t opacity, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    return object;
}

static void content_opacity_set(void *object, int32_t opacity)
{
    lv_obj_t *content = object;
    if (!content ||
        lv_obj_get_style_opa(content, LV_PART_MAIN) == (lv_opa_t)opacity) {
        return;
    }

    liquid_glass_dirty_rect_t regions[WINDOW_MAX_CONTENT_REGIONS];
    size_t region_count = 0;
    lv_obj_t *surface = lv_obj_get_parent(content);
    lv_area_t clip;
    lv_obj_get_coords(surface, &clip);
    uint32_t child_count = lv_obj_get_child_count(content);
    for (uint32_t index = 0; index < child_count; ++index) {
        lv_obj_t *child = lv_obj_get_child(content, (int32_t)index);
        if (!child || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        if (region_count >= WINDOW_MAX_CONTENT_REGIONS) {
            lv_obj_get_coords(content, &clip);
            regions[0] = (liquid_glass_dirty_rect_t) {
                .x1 = (int16_t)clip.x1, .y1 = (int16_t)clip.y1,
                .x2 = (int16_t)clip.x2, .y2 = (int16_t)clip.y2,
            };
            region_count = 1;
            break;
        }
        lv_area_t area;
        lv_obj_get_coords(child, &area);
        if (area.x1 < clip.x1) area.x1 = clip.x1;
        if (area.y1 < clip.y1) area.y1 = clip.y1;
        if (area.x2 > clip.x2) area.x2 = clip.x2;
        if (area.y2 > clip.y2) area.y2 = clip.y2;
        if (area.x1 > area.x2 || area.y1 > area.y2) continue;
        regions[region_count++] = (liquid_glass_dirty_rect_t) {
            .x1 = (int16_t)area.x1, .y1 = (int16_t)area.y1,
            .x2 = (int16_t)area.x2, .y2 = (int16_t)area.y2,
        };
    }

    lv_display_t *display = lv_obj_get_display(content);
    bool invalidation_enabled =
        display && lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled) {
        lv_display_enable_invalidation(display, false);
    }
    lv_obj_set_style_opa(content, (lv_opa_t)opacity, 0);
    if (invalidation_enabled) {
        lv_display_enable_invalidation(display, true);
        ui_glass_compositor_invalidate_visual_regions(
            s_compositor, regions, region_count);
    }
}

static void glint_progress_set(void *object, int32_t progress)
{
    for (uint8_t window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        if (s_windows[window].surface == object) {
            ui_glass_compositor_set_glint(
                s_compositor, window, (int16_t)progress);
            break;
        }
    }
    ui_glass_surface_set_glint(object, progress);
}

static void start_glint(lv_obj_t *surface, uint32_t delay)
{
    lv_anim_delete(surface, glint_progress_set);
    glint_progress_set(surface, -320);

    lv_anim_t glint;
    lv_anim_init(&glint);
    lv_anim_set_var(&glint, surface);
    lv_anim_set_exec_cb(&glint, glint_progress_set);
    lv_anim_set_values(&glint, -320, 1344);
    lv_anim_set_duration(&glint, 520);
    lv_anim_set_delay(&glint, delay);
    lv_anim_set_path_cb(&glint, lv_anim_path_ease_in_out);
    lv_anim_start(&glint);
}

static bool is_full_screen_region(const liquid_glass_dirty_rect_t *region)
{
    return region->x1 == 0 && region->y1 == 0 &&
           region->x2 == LIQUID_GLASS_COMPOSITOR_WIDTH - 1 &&
           region->y2 == LIQUID_GLASS_COMPOSITOR_HEIGHT - 1;
}

static void append_visual_region(
    liquid_glass_dirty_rect_t regions[DECK_MAX_VISUAL_REGIONS],
    size_t *count,
    lv_area_t area,
    const lv_area_t *clip)
{
    if (*count > 0 && is_full_screen_region(&regions[0])) return;
    if (clip) {
        if (area.x1 < clip->x1) area.x1 = clip->x1;
        if (area.y1 < clip->y1) area.y1 = clip->y1;
        if (area.x2 > clip->x2) area.x2 = clip->x2;
        if (area.y2 > clip->y2) area.y2 = clip->y2;
    }
    if (area.x1 < 0) area.x1 = 0;
    if (area.y1 < 0) area.y1 = 0;
    if (area.x2 >= LIQUID_GLASS_COMPOSITOR_WIDTH) {
        area.x2 = LIQUID_GLASS_COMPOSITOR_WIDTH - 1;
    }
    if (area.y2 >= LIQUID_GLASS_COMPOSITOR_HEIGHT) {
        area.y2 = LIQUID_GLASS_COMPOSITOR_HEIGHT - 1;
    }
    if (area.x1 > area.x2 || area.y1 > area.y2) return;

    if (*count >= DECK_MAX_VISUAL_REGIONS) {
        regions[0] = (liquid_glass_dirty_rect_t) {
            .x1 = 0,
            .y1 = 0,
            .x2 = LIQUID_GLASS_COMPOSITOR_WIDTH - 1,
            .y2 = LIQUID_GLASS_COMPOSITOR_HEIGHT - 1,
        };
        *count = 1;
        return;
    }
    regions[(*count)++] = (liquid_glass_dirty_rect_t) {
        .x1 = (int16_t)area.x1,
        .y1 = (int16_t)area.y1,
        .x2 = (int16_t)area.x2,
        .y2 = (int16_t)area.y2,
    };
}

static void append_surface_visual_regions(
    const window_view_t *view,
    liquid_glass_dirty_rect_t regions[DECK_MAX_VISUAL_REGIONS],
    size_t *count)
{
    lv_area_t surface;
    lv_obj_get_coords(view->surface, &surface);
    const int32_t thickness = UI_GLASS_OPTIC_RING_COUNT + 2;
    append_visual_region(regions, count, (lv_area_t) {
        .x1 = surface.x1, .y1 = surface.y1,
        .x2 = surface.x2, .y2 = surface.y1 + thickness - 1,
    }, NULL);
    append_visual_region(regions, count, (lv_area_t) {
        .x1 = surface.x1, .y1 = surface.y2 - thickness + 1,
        .x2 = surface.x2, .y2 = surface.y2,
    }, NULL);
    append_visual_region(regions, count, (lv_area_t) {
        .x1 = surface.x1, .y1 = surface.y1 + thickness,
        .x2 = surface.x1 + thickness - 1,
        .y2 = surface.y2 - thickness,
    }, NULL);
    append_visual_region(regions, count, (lv_area_t) {
        .x1 = surface.x2 - thickness + 1,
        .y1 = surface.y1 + thickness,
        .x2 = surface.x2, .y2 = surface.y2 - thickness,
    }, NULL);

    if (lv_obj_get_style_opa(view->content, LV_PART_MAIN) <= LV_OPA_MIN) {
        return;
    }
    uint32_t child_count = lv_obj_get_child_count(view->content);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t *child = lv_obj_get_child(view->content,
                                           (int32_t)child_index);
        if (!child || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t child_area;
        lv_obj_get_coords(child, &child_area);
        append_visual_region(regions, count, child_area, &surface);
    }
}

static size_t collect_deck_visual_regions(
    liquid_glass_dirty_rect_t regions[DECK_MAX_VISUAL_REGIONS],
    size_t count)
{
    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        append_surface_visual_regions(&s_windows[window], regions, &count);
    }
    return count;
}

static void window_geometry_set(void *value, int32_t progress)
{
    window_motion_t *motion = value;
    liquid_glass_frame_t frame = liquid_glass_transition_frame(
        motion->start, motion->target, motion->wraps_depth, progress);

    ui_glass_compositor_set_frame(s_compositor, motion->window_index, frame);
    lv_obj_set_pos(motion->object, frame.x, frame.y);
    lv_obj_set_size(motion->object, frame.width, frame.height);
    ui_glass_surface_set_edge_strength(motion->object,
                                       frame.border_opacity);

    // Geometry remains continuous; only ownership of the foreground plane
    // changes at the curve's depth crossing, while both cards overlap.
    if (motion->switches_depth_order && !motion->depth_order_switched &&
        progress >= LIQUID_GLASS_DEPTH_CROSS_PROGRESS) {
        order_windows();
        motion->depth_order_switched = true;
    }
}

static void deck_geometry_set(void *value, int32_t progress)
{
    deck_motion_t *deck = value;
    if (!deck || !s_screen || !s_compositor) return;

    liquid_glass_frame_t frames[LIQUID_GLASS_WINDOW_COUNT];
    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        window_motion_t *motion = &s_window_motion[window];
        frames[window] = liquid_glass_transition_frame(
            motion->start, motion->target, motion->wraps_depth, progress);
    }

    liquid_glass_dirty_rect_t visual_regions[DECK_MAX_VISUAL_REGIONS];
    size_t visual_count = collect_deck_visual_regions(visual_regions, 0);
    lv_display_t *display = lv_obj_get_display(s_screen);
    bool invalidation_enabled =
        display && lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled) {
        lv_display_enable_invalidation(display, false);
    }

    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        window_motion_t *motion = &s_window_motion[window];
        lv_obj_set_pos(motion->object, frames[window].x, frames[window].y);
        lv_obj_set_size(motion->object, frames[window].width,
                        frames[window].height);
        ui_glass_surface_set_edge_strength(
            motion->object, frames[window].border_opacity);
    }
    if (!deck->depth_order_switched &&
        progress >= LIQUID_GLASS_DEPTH_CROSS_PROGRESS) {
        order_windows();
        deck->depth_order_switched = true;
        for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
            s_window_motion[window].depth_order_switched = true;
        }
    }
    visual_count = collect_deck_visual_regions(visual_regions, visual_count);

    if (invalidation_enabled) {
        lv_display_enable_invalidation(display, true);
    }
    ui_glass_compositor_commit_frames(s_compositor, frames,
                                      visual_regions, visual_count);
}

static liquid_glass_frame_t current_frame(lv_obj_t *object)
{
    return (liquid_glass_frame_t) {
        .x = (int16_t)lv_obj_get_x(object),
        .y = (int16_t)lv_obj_get_y(object),
        .width = (int16_t)lv_obj_get_width(object),
        .height = (int16_t)lv_obj_get_height(object),
        .surface_opacity = current_optics().stack_fill_opacity,
        .border_opacity = ui_glass_surface_get_edge_strength(object),
        .content_opacity = 0,
    };
}

static void order_windows_for_selected(int selected)
{
    uint8_t draw_order[LIQUID_GLASS_WINDOW_COUNT];
    uint8_t depth = 0;
    for (int rank = LIQUID_GLASS_WINDOW_COUNT - 1; rank >= 0; --rank) {
        for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
            if (liquid_glass_window_rank((uint8_t)window,
                                         (uint8_t)selected) == rank) {
                lv_obj_move_foreground(s_windows[window].surface);
                draw_order[depth++] = (uint8_t)window;
                break;
            }
        }
    }
    ui_glass_compositor_set_draw_order(s_compositor, draw_order);
}

static void order_windows(void)
{
    order_windows_for_selected(s_selected);
}

static void deck_cycle_completed(lv_anim_t *animation)
{
    deck_motion_t *deck = lv_anim_get_user_data(animation);
    if (!s_screen || deck != &s_deck_motion) return;

    if (!deck->depth_order_switched) order_windows();
    deck->depth_order_switched = true;
    s_transitioning = false;
}

static void surface_press_set(void *object, int32_t inset)
{
    lv_obj_set_style_transform_width(object, inset, 0);
    lv_obj_set_style_transform_height(object, inset, 0);
    for (uint8_t window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        if (s_windows[window].surface != object) continue;
        liquid_glass_frame_t frame = {
            .x = (int16_t)(lv_obj_get_x(object) - inset),
            .y = (int16_t)(lv_obj_get_y(object) - inset),
            .width = (int16_t)(lv_obj_get_width(object) + inset * 2),
            .height = (int16_t)(lv_obj_get_height(object) + inset * 2),
            .surface_opacity = current_optics().stack_fill_opacity,
            .border_opacity = ui_glass_surface_get_edge_strength(object),
        };
        ui_glass_compositor_set_frame(s_compositor, window, frame);
        break;
    }
}

static void layout_windows(bool animate, bool materialize,
                           int direction, int previous_selected)
{
    // Every cycle begins with the old depth order. The wrapping card stays on
    // its original plane until the shared curve reaches its depth crossing.
    if (!materialize && direction != 0) {
        order_windows_for_selected(previous_selected);
    } else {
        order_windows();
    }
    s_transitioning = animate && !materialize && direction != 0;
    lv_anim_delete(&s_deck_motion, deck_geometry_set);
    s_deck_motion.depth_order_switched = false;
    lv_label_set_text(s_material_label, current_material().label);
    ui_glass_optics_t optics = current_optics();

    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        window_view_t *view = &s_windows[window];
        window_motion_t *motion = &s_window_motion[window];
        uint8_t rank = liquid_glass_window_rank((uint8_t)window,
                                                (uint8_t)s_selected);
        liquid_glass_frame_t settled = liquid_glass_frame_for_rank(rank);
        bool active = rank == 0;
        bool outgoing = direction != 0 && window == previous_selected;
        bool wraps_depth = direction > 0
            ? window == previous_selected
            : (direction < 0 && window == s_selected);

        // Material transmittance belongs to the whole deck, not its Z ranks.
        // Rank affects only edge exposure, geometry, content, and occlusion.
        settled.surface_opacity = optics.stack_fill_opacity;
        settled.border_opacity = edge_opacity_for_rank(optics, rank);

        lv_anim_delete(motion, window_geometry_set);
        lv_anim_delete(view->surface, surface_press_set);
        lv_anim_delete(view->surface, glint_progress_set);
        lv_anim_delete(view->content, content_opacity_set);
        surface_press_set(view->surface, 0);
        glint_progress_set(view->surface, -320);

        // The outgoing card keeps its content while being pulled away, which
        // makes it read as one physical card. Other old content retires before
        // surfaces cross; selected content returns near the end of promotion.
        lv_opa_t outgoing_content_opacity =
            lv_obj_get_style_opa(view->content, LV_PART_MAIN);
        if (!outgoing) content_opacity_set(view->content, LV_OPA_TRANSP);
        motion->object = view->surface;
        motion->window_index = (uint8_t)window;
        motion->target = settled;
        motion->wraps_depth = wraps_depth;
        motion->switches_depth_order = wraps_depth && !materialize;
        motion->depth_order_switched = false;
        if (materialize) {
            motion->start = settled;
            motion->start.x += 8;
            motion->start.y += 24;
            motion->start.width -= 16;
            motion->start.height -= 14;
            motion->start.surface_opacity = 4;
            motion->start.border_opacity = 0;
            window_geometry_set(motion, 0);
        } else {
            motion->start = current_frame(view->surface);
        }

        lv_obj_set_style_text_color(view->title,
            lv_color_hex(active ? UI_GLASS_ACCENT : UI_GLASS_TEXT_MUTED), 0);
        lv_obj_set_style_text_color(view->footer,
            lv_color_hex(UI_GLASS_ACCENT), 0);
        lv_obj_set_style_bg_color(view->dot,
            lv_color_hex(UI_GLASS_ACCENT), 0);

        if (!animate) {
            window_geometry_set(motion, LIQUID_GLASS_MOTION_PROGRESS_MAX);
            content_opacity_set(view->content, settled.content_opacity);
            continue;
        }

        if (materialize) {
            lv_anim_t geometry;
            lv_anim_init(&geometry);
            lv_anim_set_var(&geometry, motion);
            lv_anim_set_exec_cb(&geometry, window_geometry_set);
            lv_anim_set_values(&geometry, 0,
                               LIQUID_GLASS_MOTION_PROGRESS_MAX);
            lv_anim_set_duration(&geometry,
                active ? DECK_MATERIALIZE_FRONT_MS
                       : DECK_MATERIALIZE_BACK_MS);
            lv_anim_set_delay(&geometry, (2 - rank) * 38);
            LV_ANIM_SET_EASE_IN_OUT_SINE(&geometry);
            lv_anim_start(&geometry);
        }

        if (outgoing) {
            lv_anim_t retire;
            lv_anim_init(&retire);
            lv_anim_set_var(&retire, view->content);
            lv_anim_set_exec_cb(&retire, content_opacity_set);
            lv_anim_set_values(&retire, outgoing_content_opacity,
                               LV_OPA_TRANSP);
            lv_anim_set_duration(&retire, 280);
            lv_anim_set_path_cb(&retire, lv_anim_path_ease_out);
            lv_anim_start(&retire);
        }

        if (active) {
            lv_anim_t content;
            lv_anim_init(&content);
            lv_anim_set_var(&content, view->content);
            lv_anim_set_exec_cb(&content, content_opacity_set);
            lv_anim_set_values(&content, LV_OPA_TRANSP, settled.content_opacity);
            lv_anim_set_duration(&content, materialize ? 190 : 240);
            lv_anim_set_delay(&content, materialize ? 180 : 330);
            lv_anim_set_path_cb(&content, lv_anim_path_ease_out);
            lv_anim_start(&content);
            start_glint(view->surface, materialize ? 250 : 380);
        }
    }

    if (animate && !materialize) {
        lv_anim_t geometry;
        lv_anim_init(&geometry);
        lv_anim_set_var(&geometry, &s_deck_motion);
        lv_anim_set_exec_cb(&geometry, deck_geometry_set);
        lv_anim_set_values(&geometry, 0,
                           LIQUID_GLASS_MOTION_PROGRESS_MAX);
        lv_anim_set_duration(&geometry, DECK_CYCLE_DURATION_MS);
        LV_ANIM_SET_EASE_IN_OUT_SINE(&geometry);
        lv_anim_set_user_data(&geometry, &s_deck_motion);
        lv_anim_set_completed_cb(&geometry, deck_cycle_completed);
        lv_anim_start(&geometry);
    }
}

static void move_selection(int direction)
{
    if (s_transitioning) return;
    int previous = s_selected;
    s_selected = (s_selected + direction + LIQUID_GLASS_WINDOW_COUNT) %
                 LIQUID_GLASS_WINDOW_COUNT;
    layout_windows(true, false, direction, previous);
    refresh_indicators(true);
}

static void indicator_width_set(void *value, int32_t width)
{
    indicator_motion_t *motion = value;
    int32_t x = motion->center_x - width / 2;
    if (lv_obj_get_width(motion->object) == width &&
        lv_obj_get_x(motion->object) == x) {
        return;
    }

    lv_area_t old_area;
    lv_obj_get_coords(motion->object, &old_area);
    lv_display_t *display = lv_obj_get_display(motion->object);
    bool invalidation_enabled =
        display && lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled) {
        lv_display_enable_invalidation(display, false);
    }
    lv_obj_set_width(motion->object, width);
    lv_obj_set_x(motion->object, x);
    lv_area_t new_area;
    lv_obj_get_coords(motion->object, &new_area);
    if (invalidation_enabled) {
        lv_display_enable_invalidation(display, true);
        if (new_area.x1 < old_area.x1) old_area.x1 = new_area.x1;
        if (new_area.y1 < old_area.y1) old_area.y1 = new_area.y1;
        if (new_area.x2 > old_area.x2) old_area.x2 = new_area.x2;
        if (new_area.y2 > old_area.y2) old_area.y2 = new_area.y2;
        lv_obj_invalidate_area(motion->object, &old_area);
    }
}

static void refresh_indicators(bool animate)
{
    for (int i = 0; i < LIQUID_GLASS_WINDOW_COUNT; ++i) {
        indicator_motion_t *motion = &s_indicator_motion[i];
        bool active = i == s_selected;
        int32_t target_width = active ? 20 : 6;
        lv_anim_delete(motion, indicator_width_set);
        lv_obj_set_style_bg_color(motion->object,
            lv_color_hex(active ? UI_GLASS_ACCENT : UI_GLASS_TEXT_MUTED), 0);
        lv_obj_set_style_bg_opa(motion->object,
            active ? LV_OPA_80 : LV_OPA_30, 0);
        if (!animate) {
            indicator_width_set(motion, target_width);
            continue;
        }

        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, motion);
        lv_anim_set_exec_cb(&animation, indicator_width_set);
        lv_anim_set_values(&animation, lv_obj_get_width(motion->object),
                           target_width);
        lv_anim_set_duration(&animation, 270);
        lv_anim_set_path_cb(&animation, lv_anim_path_overshoot);
        lv_anim_start(&animation);
    }
}

static void activity_dot_size_set(void *object, int32_t size)
{
    lv_obj_set_size(object, size, size);
    lv_obj_set_pos(object, 20 - size / 2, 20 - size / 2);
}

static void border_opacity_set(void *object, int32_t opacity)
{
    for (uint8_t window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        if (s_windows[window].surface == object) {
            ui_glass_compositor_set_edge_strength(
                s_compositor, window, (uint8_t)opacity);
            break;
        }
    }
    ui_glass_surface_set_edge_strength(object, (uint8_t)opacity);
}

static void bar_height_set(void *value, int32_t height)
{
    bar_motion_t *motion = value;
    lv_obj_set_height(motion->object, height);
    lv_obj_set_y(motion->object, motion->center_y - height / 2);
}

static void pulse_active_window(void)
{
    window_view_t *active = &s_windows[s_selected];
    ui_glass_optics_t optics = current_optics();

    lv_anim_delete(active->dot, activity_dot_size_set);
    lv_anim_delete(active->surface, border_opacity_set);
    lv_anim_delete(active->surface, surface_press_set);

    lv_anim_t press;
    lv_anim_init(&press);
    lv_anim_set_var(&press, active->surface);
    lv_anim_set_exec_cb(&press, surface_press_set);
    lv_anim_set_values(&press, 0, -3);
    lv_anim_set_duration(&press, 85);
    lv_anim_set_playback_duration(&press, 190);
    lv_anim_set_path_cb(&press, lv_anim_path_ease_out);
    lv_anim_start(&press);

    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, active->dot);
    lv_anim_set_exec_cb(&pulse, activity_dot_size_set);
    lv_anim_set_values(&pulse, 7, 13);
    lv_anim_set_duration(&pulse, 90);
    lv_anim_set_playback_duration(&pulse, 210);
    lv_anim_set_path_cb(&pulse, lv_anim_path_overshoot);
    lv_anim_start(&pulse);

    lv_anim_t rim;
    lv_anim_init(&rim);
    lv_anim_set_var(&rim, active->surface);
    lv_anim_set_exec_cb(&rim, border_opacity_set);
    lv_anim_set_values(&rim, optics.edge_strength,
                       optics.edge_strength + 64);
    lv_anim_set_duration(&rim, 95);
    lv_anim_set_playback_duration(&rim, 230);
    lv_anim_set_path_cb(&rim, lv_anim_path_ease_out);
    lv_anim_start(&rim);

    for (int bar = 0; bar < WINDOW_BAR_COUNT; ++bar) {
        bar_motion_t *motion = &s_bar_motion[s_selected][bar];
        lv_anim_delete(motion, bar_height_set);
        bar_height_set(motion, motion->resting_height);

        int32_t peak = motion->resting_height + 5 + (bar % 3);
        lv_anim_t ripple;
        lv_anim_init(&ripple);
        lv_anim_set_var(&ripple, motion);
        lv_anim_set_exec_cb(&ripple, bar_height_set);
        lv_anim_set_values(&ripple, motion->resting_height, peak);
        lv_anim_set_duration(&ripple, 100);
        lv_anim_set_playback_duration(&ripple, 190);
        lv_anim_set_delay(&ripple, (uint32_t)bar * 28);
        lv_anim_set_path_cb(&ripple, lv_anim_path_overshoot);
        lv_anim_start(&ripple);
    }

    start_glint(active->surface, 35);
}

static void apply_material(void)
{
    deck_material_t mode = current_material();
    ui_glass_optics_t optics = current_optics();
    lv_label_set_text(s_material_label, mode.label);

    const uint8_t fill_opacity[LIQUID_GLASS_WINDOW_COUNT] = {
        optics.stack_fill_opacity,
        optics.stack_fill_opacity,
        optics.stack_fill_opacity,
    };
    ui_glass_compositor_set_material(s_compositor, 0x526677, mode.material,
                                     fill_opacity);

    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        uint8_t rank = liquid_glass_window_rank((uint8_t)window,
                                                (uint8_t)s_selected);
        lv_obj_t *surface = s_windows[window].surface;
        lv_anim_delete(surface, border_opacity_set);
        ui_glass_surface_set_material(surface, mode.material);
        uint8_t edge_strength = edge_opacity_for_rank(optics, rank);
        ui_glass_compositor_set_edge_strength(
            s_compositor, (uint8_t)window, edge_strength);
        ui_glass_surface_set_edge_strength(surface, edge_strength);
    }
}

static void cycle_material(void)
{
    if (s_transitioning) return;
    s_material_index = (s_material_index + 1) % DECK_MATERIAL_COUNT;
    apply_material();
    pulse_active_window();
}

static void stop_showcase(void)
{
    if (!s_showcase_timer) return;
    lv_timer_delete(s_showcase_timer);
    s_showcase_timer = NULL;
}

// The review tour explicitly changes material and card on separate beats, so
// their independent state axes remain legible. Any physical key cancels it.
static void showcase_tick(lv_timer_t *timer)
{
    (void)timer;
    switch (s_showcase_stage++) {
    case 0:
        pulse_active_window();
        break;
    case 1:
        cycle_material();
        break;
    case 2:
        move_selection(1);
        break;
    case 3:
        cycle_material();
        break;
    default:
        move_selection(1);
        stop_showcase();
        break;
    }
}

static void create_window(int index)
{
    const window_content_t *content = &WINDOW_CONTENT[index];
    window_view_t *view = &s_windows[index];
    deck_material_t mode = current_material();

    view->surface = ui_glass_surface_create(s_screen, 26, 116, 188, 148,
                                             26, 0x526677,
                                             LV_OPA_TRANSP,
                                             mode.material);
    ui_glass_surface_set_fused_edge(view->surface, true);
    view->content = flat_object(view->surface, 0, 0, 212, 172,
                                0, LV_OPA_TRANSP, 0);
    view->dot = flat_object(view->content, 17, 17, 7, 7,
                            UI_GLASS_ACCENT, LV_OPA_80, LV_RADIUS_CIRCLE);
    view->title = ui_glass_label(view->content, content->title,
                                 &lv_font_montserrat_14, UI_GLASS_ACCENT);
    lv_obj_set_pos(view->title, 32, 11);

    lv_obj_t *number = ui_glass_label(view->content, content->number,
                                      &lv_font_montserrat_14,
                                      UI_GLASS_TEXT_MUTED);
    lv_obj_set_pos(number, 174, 11);

    view->value = ui_glass_label(view->content, content->value,
                                 &lv_font_montserrat_20, UI_GLASS_TEXT);
    lv_obj_set_pos(view->value, 17, 50);

    view->detail = ui_glass_label(view->content, content->detail,
                                  &lv_font_montserrat_14,
                                  UI_GLASS_TEXT_MUTED);
    lv_obj_set_pos(view->detail, 17, 78);

    for (int bar = 0; bar < WINDOW_BAR_COUNT; ++bar) {
        int height = content->bars[bar];
        view->bars[bar] = flat_object(view->content, 17 + bar * 18,
                                      119 + (26 - height) / 2,
                                      12, height, UI_GLASS_ACCENT,
                                      (lv_opa_t)(76 + bar * 14), 6);
        s_bar_motion[index][bar] = (bar_motion_t) {
            .object = view->bars[bar],
            .center_y = 132,
            .resting_height = (int16_t)height,
        };
    }

    view->footer = ui_glass_label(view->content, content->footer,
                                  &lv_font_montserrat_14, UI_GLASS_ACCENT);
    lv_obj_set_pos(view->footer, 17, 143);
}

void demo_liquid_glass_enter(void)
{
    s_selected = 0;
    s_material_index = 0;
    s_screen = ui_glass_screen_create();
    s_refresh_timer = lv_display_get_refr_timer(
        lv_obj_get_display(s_screen));
    if (s_refresh_timer) {
        lv_timer_set_period(s_refresh_timer, DECK_REFRESH_PERIOD_MS);
    }
    s_compositor = ui_glass_compositor_create(
        s_screen, 0x526677, current_material().material);

    lv_obj_t *brand = ui_glass_label(s_screen, "Passport",
                                     &lv_font_montserrat_20, UI_GLASS_TEXT);
    lv_obj_set_pos(brand, 16, 14);

    s_material_label = ui_glass_label(s_screen,
                                      current_material().label,
                                      &lv_font_montserrat_14,
                                      UI_GLASS_TEXT_MUTED);
    lv_obj_set_pos(s_material_label, 17, 43);

    lv_obj_t *battery_surface = ui_glass_surface_create(
        s_screen, 174, 12, 50, 28, 14, 0xDCE8EF, LV_OPA_10,
        UI_GLASS_MATERIAL_CHROME);
    s_battery = ui_glass_label(battery_surface, "--%",
                               &lv_font_montserrat_14, UI_GLASS_TEXT);
    if (s_battery_soc >= 0) {
        lv_label_set_text_fmt(s_battery, "%d%%", s_battery_soc);
    }
    lv_obj_center(s_battery);

    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        create_window(window);
    }

    static const int16_t centers[LIQUID_GLASS_WINDOW_COUNT] = { 102, 120, 138 };
    for (int i = 0; i < LIQUID_GLASS_WINDOW_COUNT; ++i) {
        s_indicator_motion[i].center_x = centers[i];
        s_indicator_motion[i].object = flat_object(
            s_screen, centers[i] - 3, 267, 6, 6,
            UI_GLASS_TEXT_MUTED, LV_OPA_30, LV_RADIUS_CIRCLE);
    }

    lv_obj_t *browse_hint = ui_glass_label(s_screen, "UP/DN  browse",
                                            &lv_font_montserrat_14,
                                            UI_GLASS_TEXT_MUTED);
    lv_obj_set_pos(browse_hint, 16, 292);
    lv_obj_set_style_text_opa(browse_hint, LV_OPA_50, 0);

    lv_obj_t *feel_hint = ui_glass_label(s_screen, "OK  material",
                                          &lv_font_montserrat_14,
                                          UI_GLASS_TEXT_MUTED);
    lv_obj_set_pos(feel_hint, 140, 292);
    lv_obj_set_style_text_opa(feel_hint, LV_OPA_50, 0);

    lv_screen_load(s_screen);
    layout_windows(true, true, 0, s_selected);
    refresh_indicators(false);
    s_showcase_stage = 0;
    s_showcase_timer = lv_timer_create(showcase_tick, 1300, NULL);
}

void demo_liquid_glass_exit(void)
{
    if (!s_screen) return;
    if (s_refresh_timer) {
        lv_timer_set_period(s_refresh_timer, LV_DEF_REFR_PERIOD);
        s_refresh_timer = NULL;
    }
    stop_showcase();
    lv_anim_delete(&s_deck_motion, deck_geometry_set);

    for (int window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
        lv_anim_delete(&s_window_motion[window], window_geometry_set);
        lv_anim_delete(&s_indicator_motion[window], indicator_width_set);
        lv_anim_delete(s_windows[window].dot, activity_dot_size_set);
        lv_anim_delete(s_windows[window].surface, border_opacity_set);
        lv_anim_delete(s_windows[window].surface, surface_press_set);
        lv_anim_delete(s_windows[window].surface, glint_progress_set);
        lv_anim_delete(s_windows[window].content, content_opacity_set);
        for (int bar = 0; bar < WINDOW_BAR_COUNT; ++bar) {
            lv_anim_delete(&s_bar_motion[window][bar], bar_height_set);
        }
    }
    lv_obj_delete(s_screen);

    s_screen = NULL;
    s_battery = NULL;
    s_material_label = NULL;
    s_compositor = NULL;
    memset(s_windows, 0, sizeof(s_windows));
    memset(s_window_motion, 0, sizeof(s_window_motion));
    memset(&s_deck_motion, 0, sizeof(s_deck_motion));
    memset(s_indicator_motion, 0, sizeof(s_indicator_motion));
    memset(s_bar_motion, 0, sizeof(s_bar_motion));
}

void demo_liquid_glass_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (event != BSP_BTN_CLICK) return;
    stop_showcase();

    if (button == BSP_BTN_UP) {
        move_selection(-1);
    } else if (button == BSP_BTN_DOWN) {
        move_selection(1);
    } else if (button == BSP_BTN_OK) {
        cycle_material();
    }
}
