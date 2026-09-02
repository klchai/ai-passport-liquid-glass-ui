#include "demo.h"

#include "ui_glass.h"
#include "ui_glass_focus.h"
#include "ui_glass_motion.h"
#include "ui_glass_runtime.h"
#include "ui_glass_widgets.h"

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SHOWCASE_PAGE_COUNT       8
#define SHOWCASE_SCENE_Y         44
#define SHOWCASE_SCENE_HEIGHT   276
// The unattended reel is meant to be watched, not benchmarked. Seven seconds
// leaves time to read the scene after its signature interactions have played.
#define SHOWCASE_TOUR_PERIOD_MS 7000
#define SHOWCASE_SCENE_STEP_MS  1000
#define SHOWCASE_MAX_FOCUS        6

typedef enum {
    SHOWCASE_BUTTONS = 0,
    SHOWCASE_SELECTION,
    SHOWCASE_ADJUSTMENTS,
    SHOWCASE_LISTS,
    SHOWCASE_OVERLAYS,
    SHOWCASE_NAVIGATION,
    SHOWCASE_FEEDBACK,
    SHOWCASE_STATES,
} showcase_page_t;

typedef struct {
    lv_obj_t *outgoing;
    lv_obj_t *incoming;
    int8_t direction;
} page_transition_t;

typedef struct {
    lv_obj_t *lens;
    int16_t start_y;
    int16_t target_y;
} focus_motion_t;

typedef struct {
    lv_obj_t *surface;
    lv_obj_t *shadow;
    lv_obj_t *context;
    lv_obj_t *button_label;
    lv_obj_t *dimmer;
    lv_obj_t *menu;
    lv_obj_t *highlight;
    lv_obj_t *menu_rows[3];
    ui_glass_morph_frame_t from;
    ui_glass_morph_frame_t to;
    uint32_t base_tint;
    bool target_open;
    bool open;
    bool animating;
    uint8_t item;
    uint8_t visual_depth;
} morph_view_t;

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *dock;
    lv_obj_t *dock_shadow;
    lv_obj_t *selection;
    lv_obj_t *selection_shadow;
    lv_obj_t *content;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *hero;
    lv_obj_t *symbol;
    lv_obj_t *state_label;
    lv_obj_t *value_label;
    lv_obj_t *tab_items[3];
    int16_t start_x;
    int16_t target_x;
    int8_t direction;
    uint8_t target_index;
    bool content_swapped;
    bool animating;
} navigation_view_t;

typedef struct {
    lv_obj_t *indicator;
    int16_t start_x;
    int16_t target_x;
} segment_motion_t;

typedef struct {
    lv_obj_t *status_chip;
    lv_obj_t *status_label;
    lv_obj_t *alert;
    lv_obj_t *alert_title;
    lv_obj_t *alert_detail;
    ui_glass_component_t progress;
    lv_obj_t *toast_label;
} feedback_view_t;

static const char *const PAGE_TITLES[SHOWCASE_PAGE_COUNT] = {
    "Moments",
    "Focus",
    "Controls",
    "Devices",
    "Player",
    "Home",
    "Activity",
    "Appearance",
};

// The public reel starts with the two strongest content-first scenes, then
// unfolds the interaction patterns that power them. Stable enum identities let
// component state survive a reordered presentation sequence.
static const showcase_page_t PAGE_ORDER[SHOWCASE_PAGE_COUNT] = {
    SHOWCASE_OVERLAYS,
    SHOWCASE_NAVIGATION,
    SHOWCASE_SELECTION,
    SHOWCASE_ADJUSTMENTS,
    SHOWCASE_LISTS,
    SHOWCASE_FEEDBACK,
    SHOWCASE_BUTTONS,
    SHOWCASE_STATES,
};

static const char *const PAGE_SECONDARY_ACTION[SHOWCASE_PAGE_COUNT] = {
    "MOVE", "MOVE", "MOVE", "MOVE",
    "MENU", "NEXT", "STATE", "MOVE",
};

static const char *const PAGE_PRIMARY_ACTION[SHOWCASE_PAGE_COUNT] = {
    "OPEN", "SET", "ADJUST", "OPEN",
    "PLAY", "OPEN", "RUN", "APPLY",
};

static ui_glass_runtime_t s_runtime;
static lv_obj_t *s_scene;
static lv_obj_t *s_header_chrome;
static lv_obj_t *s_header_title;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_footer;
static lv_obj_t *s_footer_label;
static lv_timer_t *s_tour_timer;
static lv_timer_t *s_scene_timer;
static page_transition_t s_page_motion;
static focus_motion_t s_focus_motion;
static morph_view_t s_morph;
static showcase_page_t s_page;
static bool s_transitioning;
static uint8_t s_tour_steps;
static uint8_t s_scene_step;
static int s_battery_soc = -1;
static int s_presented_battery_soc = -2;
static segment_motion_t s_segment_motion;
static lv_obj_t *s_player_state_label;
static bool s_player_playing = true;
static feedback_view_t s_feedback;

static ui_glass_focus_model_t s_focus;
static lv_obj_t *s_focus_lens;
static ui_glass_component_t s_focus_components[SHOWCASE_MAX_FOCUS];
static int16_t s_focus_y[SHOWCASE_MAX_FOCUS];
static uint8_t s_focus_count;

static uint8_t s_segment_index;
static bool s_control_toggle = true;
static bool s_selection_check = true;
static bool s_selection_radio = true;
static uint8_t s_control_slider = 68;
static uint8_t s_stepper_value = 3;
static uint8_t s_navigation_index;
static uint8_t s_device_volume = 70;
static uint8_t s_feedback_state;
static navigation_view_t s_navigation;

static void navigation_motion_set(void *value, int32_t progress);
static void segment_motion_set(void *value, int32_t progress);
static void focused_action(void);

static lv_obj_t *plain_object(lv_obj_t *parent, int x, int y,
                              int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    return object;
}

static lv_obj_t *solid_object(lv_obj_t *parent, int x, int y,
                              int width, int height, int radius,
                              uint32_t color, lv_opa_t opacity)
{
    lv_obj_t *object = plain_object(parent, x, y, width, height);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    return object;
}

static lv_obj_t *text_at(lv_obj_t *parent, const char *text,
                         int x, int y, const lv_font_t *font,
                         uint32_t color)
{
    lv_obj_t *label = ui_glass_label(parent, text, font, color);
    lv_obj_set_pos(label, x, y);
    return label;
}

static const ui_glass_theme_t *theme(void)
{
    return ui_glass_runtime_theme(&s_runtime);
}

static uint8_t page_position(showcase_page_t page)
{
    for (uint8_t index = 0; index < SHOWCASE_PAGE_COUNT; ++index) {
        if (PAGE_ORDER[index] == page) return index;
    }
    return 0;
}

static void set_label_color(lv_obj_t *label, uint32_t color)
{
    if (label) lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

static uint8_t opacity_clamp(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static lv_obj_t *content_layer_create(lv_obj_t *parent, int x, int y,
                                      int width, int height,
                                      const ui_glass_theme_t *t)
{
    (void)t;
    // Content stays on the wallpaper plane. Only controls and transient
    // presentation surfaces receive glass material.
    return plain_object(parent, x, y, width, height);
}

static lv_opa_t content_canvas_opacity(void)
{
    switch (s_runtime.mode) {
    case UI_GLASS_MODE_HIGH_CONTRAST:
        return 224;
    case UI_GLASS_MODE_REDUCED_TRANSPARENCY:
        return LV_OPA_COVER;
    case UI_GLASS_MODE_STANDARD:
    case UI_GLASS_MODE_REDUCED_MOTION:
    default:
        return 96;
    }
}

static lv_opa_t content_group_opacity(void)
{
    return s_runtime.mode == UI_GLASS_MODE_STANDARD ||
                   s_runtime.mode == UI_GLASS_MODE_REDUCED_MOTION
               ? 190
               : LV_OPA_COVER;
}

static lv_obj_t *showcase_content_stage_create(
    lv_obj_t *root, const ui_glass_theme_t *t)
{
    // Quiet the photographic wallpaper edge-to-edge instead of placing every
    // scene in the same rounded card. The footer remains above the undimmed
    // wallpaper so its own glass transmission stays visible.
    solid_object(root, 0, 0, LIQUID_GLASS_COMPOSITOR_WIDTH, 228, 0,
                 t->content_surface, content_canvas_opacity());
    return content_layer_create(root, 16, 4, 208, 204, t);
}

static void content_divider_create(lv_obj_t *parent, int y,
                                   const ui_glass_theme_t *t)
{
    solid_object(parent, 20, y, 168, 1, 0,
                 t->text_muted, LV_OPA_20);
}

static void reference_glass_apply(lv_obj_t *surface, uint32_t base_tint,
                                  uint8_t opacity, uint8_t depth)
{
    if (!surface) return;
    if (depth > 4) depth = 4;
    uint32_t top = ui_glass_mix_rgb(base_tint, 0xE9F8FF,
                                    (uint8_t)(28 + depth * 5));
    uint32_t bottom = ui_glass_mix_rgb(base_tint, 0x03101C,
                                       (uint8_t)(34 + depth * 4));
    uint32_t fill = ui_glass_mix_rgb(top, bottom, 116);
    ui_glass_surface_set_tint(surface, fill,
                              opacity_clamp(opacity + depth * 2));
    // LVGL's software blur shadow starves the idle task on this no-PSRAM C3.
    // Depth is represented by cheap offset silhouettes owned by the scene.
    lv_obj_set_style_shadow_width(surface, 0, 0);
    // Low fill opacity needs a stronger optical rim to read as a material
    // boundary instead of a washed-out rectangle. The optics profile keeps
    // the highlight local, so this does not restore the old double white rule.
    ui_glass_surface_set_edge_strength(surface, 78 + depth * 8);
}

static lv_obj_t *reference_glass_create(lv_obj_t *parent,
                                        int x, int y, int width, int height,
                                        int radius, uint8_t opacity,
                                        uint8_t depth,
                                        const ui_glass_theme_t *t,
                                        uint32_t *base_tint)
{
    uint32_t borrowed = ui_glass_background_at_y(
        (int16_t)(SHOWCASE_SCENE_Y + y + height / 2));
    uint32_t tint = ui_glass_mix_rgb(borrowed, t->control_tint, 164);
    lv_obj_t *surface = ui_glass_surface_create(
        parent, x, y, width, height, radius, tint, opacity,
        UI_GLASS_MATERIAL_REGULAR);
    reference_glass_apply(surface, tint, opacity, depth);
    if (base_tint) *base_tint = tint;
    return surface;
}

static void stop_tour(void)
{
    if (!s_tour_timer) return;
    lv_timer_delete(s_tour_timer);
    s_tour_timer = NULL;
}

static void stop_scene_timer(void)
{
    if (!s_scene_timer) return;
    lv_timer_delete(s_scene_timer);
    s_scene_timer = NULL;
}

static void focus_lens_set(void *value, int32_t progress)
{
    focus_motion_t *motion = value;
    if (!motion || !motion->lens) return;
    int32_t eased = ui_glass_spring(progress);
    lv_obj_set_y(motion->lens,
                 ui_glass_interpolate(motion->start_y,
                                      motion->target_y, eased));
}

static void stop_scene_activity(void)
{
    stop_scene_timer();
    lv_anim_delete(&s_focus_motion, focus_lens_set);
    s_focus_motion.lens = NULL;
    lv_anim_delete(&s_segment_motion, segment_motion_set);
    memset(&s_segment_motion, 0, sizeof(s_segment_motion));
    if (s_morph.surface) {
        lv_anim_delete(&s_morph, NULL);
    }
    lv_anim_delete(&s_navigation, navigation_motion_set);
    memset(&s_morph, 0, sizeof(s_morph));
    memset(&s_navigation, 0, sizeof(s_navigation));
    memset(&s_feedback, 0, sizeof(s_feedback));
    s_focus_lens = NULL;
    s_focus_count = 0;
    s_player_state_label = NULL;
    memset(s_focus_components, 0, sizeof(s_focus_components));
}

static void focus_refresh_states(void)
{
    for (uint8_t i = 0; i < s_focus_count; ++i) {
        ui_glass_component_set_state(
            &s_focus_components[i],
            i == s_focus.index ? UI_GLASS_COMPONENT_FOCUSED
                               : UI_GLASS_COMPONENT_DEFAULT,
            theme());
    }
}

static void focus_bind(lv_obj_t *lens, uint8_t count, uint8_t initial)
{
    s_focus_lens = lens;
    s_focus_count = count;
    ui_glass_focus_init(&s_focus, count, initial, true);
    if (lens && count > 0) lv_obj_set_y(lens, s_focus_y[s_focus.index]);
    focus_refresh_states();
}

static void focus_move(int8_t delta)
{
    if (!s_focus_lens || !ui_glass_focus_move(&s_focus, delta)) return;
    focus_refresh_states();
    // Rapid button input retargets the one physical lens from its current
    // sampled position instead of allowing two animations to fight over it.
    lv_anim_delete(&s_focus_motion, focus_lens_set);
    s_focus_motion = (focus_motion_t) {
        .lens = s_focus_lens,
        .start_y = lv_obj_get_y(s_focus_lens),
        .target_y = s_focus_y[s_focus.index],
    };
    uint16_t duration = ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_FOCUS);
    if (duration == 0) {
        focus_lens_set(&s_focus_motion, UI_GLASS_MOTION_PROGRESS_MAX);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &s_focus_motion);
    lv_anim_set_exec_cb(&animation, focus_lens_set);
    lv_anim_set_values(&animation, 0, UI_GLASS_MOTION_PROGRESS_MAX);
    lv_anim_set_duration(&animation, duration);
    lv_anim_start(&animation);
}

static void press_set(void *object, int32_t inset)
{
    lv_obj_t *target = object;
    if (!target) return;
    lv_obj_set_style_transform_width(target, -inset, 0);
    lv_obj_set_style_transform_height(target, -inset, 0);
}

static void width_set(void *object, int32_t width)
{
    if (object) lv_obj_set_width((lv_obj_t *)object, width);
}

static void animate_width(lv_obj_t *object, int32_t target_width)
{
    if (!object) return;
    lv_anim_delete(object, width_set);
    uint16_t duration = ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_FOCUS);
    if (duration == 0) {
        lv_obj_set_width(object, target_width);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, width_set);
    lv_anim_set_values(&animation, lv_obj_get_width(object), target_width);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void press_focused_component(void)
{
    if (s_focus.index >= s_focus_count) return;
    lv_obj_t *object = s_focus_components[s_focus.index].root;
    if (!object) return;
    lv_anim_delete(object, press_set);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, press_set);
    lv_anim_set_values(&animation, 0, 3);
    lv_anim_set_duration(&animation, ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_PRESS));
    lv_anim_set_playback_duration(&animation, 150);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text,
                                const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = ui_glass_label(parent, text, font, color);
    // Montserrat's line box is geometrically centered but its visible caps sit
    // low inside short pills. A one-pixel optical correction keeps alphabetic
    // labels and +/- glyphs centered on this 240x320 panel.
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -1);
    return label;
}

static ui_glass_component_t showcase_button_create(
    lv_obj_t *parent, int y, const char *label, uint8_t style,
    const ui_glass_theme_t *t)
{
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, 6, y, 196, 40);
    lv_obj_t *button;
    uint32_t text_color = t->text;
    if (style == 0) {
        button = solid_object(component.root, 6, 1, 184, 38, 14,
                              t->accent, LV_OPA_COVER);
        text_color = t->content_surface;
    } else if (style == 1) {
        button = ui_glass_surface_create(
            component.root, 6, 1, 184, 38, 14, t->control_tint,
            t->control_opacity, t->control_material);
    } else if (style == 2) {
        button = solid_object(component.root, 6, 1, 184, 38, 14,
                              t->danger, LV_OPA_30);
        text_color = t->danger;
    } else {
        button = solid_object(component.root, 6, 1, 184, 38, 14,
                              t->text_muted, LV_OPA_20);
        text_color = t->text_muted;
        lv_obj_set_style_opa(button, LV_OPA_50, 0);
    }
    centered_label(button, label, &lv_font_montserrat_14, text_color);
    component.indicator = button;
    return component;
}

static ui_glass_component_t showcase_segmented_create(
    lv_obj_t *parent, int y, const ui_glass_theme_t *t)
{
    static const char *const labels[] = { "Work", "Rest", "Off" };
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, 6, y, 196, 42);
    lv_obj_t *platter = ui_glass_surface_create(
        component.root, 6, 2, 184, 38, 18, t->control_tint,
        t->control_opacity, t->control_material);
    component.auxiliary = platter;
    component.indicator = solid_object(
        platter, 4 + s_segment_index * 59, 4, 58, 30,
        15, t->accent, LV_OPA_40);
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_t *label = text_at(platter, labels[i], 4 + i * 59, 10,
                                  &lv_font_montserrat_14,
                                  i == s_segment_index
                                      ? t->text : t->text_muted);
        lv_obj_set_width(label, 58);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    return component;
}

static void segment_motion_set(void *value, int32_t progress)
{
    segment_motion_t *motion = value;
    if (!motion || !motion->indicator) return;
    int32_t eased = ui_glass_spring(progress);
    lv_obj_set_x(motion->indicator,
                 ui_glass_interpolate(motion->start_x,
                                      motion->target_x, eased));
}

static void segment_select(uint8_t index, bool animate)
{
    if (index > 2 || !s_focus_components[0].indicator ||
        !s_focus_components[0].auxiliary) {
        return;
    }
    s_segment_index = index;
    const ui_glass_theme_t *t = theme();
    for (uint8_t item = 0; item < 3; ++item) {
        lv_obj_t *label = lv_obj_get_child(
            s_focus_components[0].auxiliary, (int32_t)item + 1);
        set_label_color(label, item == index ? t->text : t->text_muted);
    }

    lv_anim_delete(&s_segment_motion, segment_motion_set);
    s_segment_motion = (segment_motion_t) {
        .indicator = s_focus_components[0].indicator,
        .start_x = lv_obj_get_x(s_focus_components[0].indicator),
        .target_x = (int16_t)(4 + index * 59),
    };
    uint16_t duration = animate ? ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_FOCUS) : 0;
    if (duration == 0) {
        segment_motion_set(&s_segment_motion, UI_GLASS_MOTION_PROGRESS_MAX);
        return;
    }

    lv_anim_t motion;
    lv_anim_init(&motion);
    lv_anim_set_var(&motion, &s_segment_motion);
    lv_anim_set_exec_cb(&motion, segment_motion_set);
    lv_anim_set_values(&motion, 0, UI_GLASS_MOTION_PROGRESS_MAX);
    lv_anim_set_duration(&motion, duration);
    lv_anim_start(&motion);
}

static ui_glass_component_t showcase_choice_create(
    lv_obj_t *parent, int y, const char *label, bool selected,
    bool radio, const ui_glass_theme_t *t)
{
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, 6, y, 196, 42);
    component.label = text_at(component.root, label, 48, 12,
                              &lv_font_montserrat_14, t->text);
    lv_obj_t *mark = solid_object(
        component.root, 14, 10, 22, 22, radio ? LV_RADIUS_CIRCLE : 6,
        selected ? t->accent : t->text_muted,
        selected ? LV_OPA_COVER : LV_OPA_30);
    if (selected) {
        solid_object(mark, radio ? 6 : 5, radio ? 6 : 5,
                     radio ? 10 : 12, radio ? 10 : 12,
                     radio ? LV_RADIUS_CIRCLE : 3,
                     t->content_surface, LV_OPA_COVER);
    }
    component.indicator = mark;
    return component;
}

static ui_glass_component_t showcase_stepper_create(
    lv_obj_t *parent, int y, const ui_glass_theme_t *t)
{
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, 6, y, 196, 44);
    component.label = text_at(component.root, "Depth", 14, 13,
                              &lv_font_montserrat_14, t->text);
    lv_obj_t *minus = ui_glass_surface_create(
        component.root, 108, 7, 30, 30, 12, t->control_tint,
        t->control_opacity, t->control_material);
    lv_obj_t *plus = ui_glass_surface_create(
        component.root, 160, 7, 30, 30, 12, t->control_tint,
        t->control_opacity, t->control_material);
    centered_label(minus, "-", &lv_font_montserrat_14, t->text);
    centered_label(plus, "+", &lv_font_montserrat_14, t->text);
    component.value = text_at(component.root, "0", 143, 13,
                              &lv_font_montserrat_14, t->text);
    lv_label_set_text_fmt(component.value, "%u", s_stepper_value);
    return component;
}

static ui_glass_component_t showcase_progress_create(
    lv_obj_t *parent, int y, const char *label, uint8_t percent,
    const ui_glass_theme_t *t)
{
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, 6, y, 196, 48);
    component.label = text_at(component.root, label, 14, 3,
                              &lv_font_montserrat_14, t->text);
    component.value = text_at(component.root, "0%", 154, 3,
                              &lv_font_montserrat_14, t->text_muted);
    lv_label_set_text_fmt(component.value, "%u%%", percent);
    lv_obj_t *track = solid_object(component.root, 14, 29, 168, 6,
                                   LV_RADIUS_CIRCLE, t->text_muted, LV_OPA_20);
    component.indicator = solid_object(
        track, 0, 0, 168 * percent / 100, 6, LV_RADIUS_CIRCLE,
        t->accent, LV_OPA_COVER);
    return component;
}

static void build_buttons(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = showcase_content_stage_create(root, t);
    lv_obj_t *art = solid_object(stage, 6, 6, 196, 70, 16,
                                 0x174B68, LV_OPA_COVER);
    solid_object(art, 128, -18, 82, 82, LV_RADIUS_CIRCLE,
                 0x5AC8E8, LV_OPA_40);
    solid_object(art, -12, 42, 132, 42, 18,
                 0x343873, LV_OPA_80);
    text_at(art, "Night Drive", 14, 12,
            &lv_font_montserrat_20, t->text);
    text_at(art, "12 moments", 15, 42,
            &lv_font_montserrat_14, t->text_muted);

    // Follow the action capsule's actual 184 px geometry with only a two-pixel
    // focus halo. The previous 196 px lens produced a visibly nested pill.
    lv_obj_t *lens = ui_glass_focus_lens_create(stage, 10, 79, 188, 42, t);
    static const char *const labels[] = {
        "Open moment", "Share", "Remove", NULL,
    };
    for (uint8_t i = 0; i < 3; ++i) {
        s_focus_y[i] = 79 + i * 40;
        s_focus_components[i] = showcase_button_create(
            stage, s_focus_y[i], labels[i], i, t);
    }
    focus_bind(lens, 3, 0);
}

static void build_selection(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = showcase_content_stage_create(root, t);
    // The first row already contains a glass segmented platter. Keep the
    // shared focus halo close to that platter instead of drawing a second,
    // substantially wider capsule around it.
    lv_obj_t *lens = ui_glass_focus_lens_create(stage, 10, 6, 188, 42, t);
    content_divider_create(stage, 51, t);
    content_divider_create(stage, 99, t);
    content_divider_create(stage, 147, t);
    s_focus_y[0] = 6;
    s_focus_y[1] = 54;
    s_focus_y[2] = 102;
    s_focus_y[3] = 150;
    s_focus_components[0] = showcase_segmented_create(stage, 6, t);
    s_focus_components[1] = ui_glass_toggle_create(
        stage, 6, 54, 196, 42, "Silence alerts", s_control_toggle, t);
    s_focus_components[2] = showcase_choice_create(
        stage, 102, "Dim wallpaper", s_selection_check, false, t);
    s_focus_components[3] = showcase_choice_create(
        stage, 150, "Auto resume", s_selection_radio, true, t);
    focus_bind(lens, 4, 0);
}

static void build_adjustments(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = showcase_content_stage_create(root, t);
    // These are semantic control groups, not a page-sized glass card. Their
    // stable fill keeps fine slider/progress geometry legible over the image.
    solid_object(stage, 6, 4, 196, 104, 18,
                 t->content_surface, content_group_opacity());
    solid_object(stage, 6, 116, 196, 72, 18,
                 t->content_surface, content_group_opacity());
    content_divider_create(stage, 58, t);
    lv_obj_t *lens = ui_glass_focus_lens_create(stage, 6, 8, 196, 44, t);
    s_focus_y[0] = 8;
    s_focus_y[1] = 64;
    s_focus_y[2] = 124;
    s_focus_components[0] = ui_glass_slider_create(
        stage, 6, 8, 196, 44, "Brightness", s_control_slider, t);
    s_focus_components[1] = showcase_stepper_create(stage, 64, t);
    s_focus_components[2] = showcase_progress_create(
        stage, 124, "Ambient level", s_device_volume, t);
    focus_bind(lens, 3, 0);
}

static void build_lists(lv_obj_t *root)
{
    static const char *const labels[] = {
        "Passport Buds", "Studio Display", "MacBook", "Keyboard",
    };
    static const char *const values[] = {
        "LINKED", "NEARBY", "SLEEP", "OFFLINE",
    };
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = showcase_content_stage_create(root, t);
    lv_obj_t *lens = ui_glass_focus_lens_create(stage, 6, 10, 196, 42, t);
    content_divider_create(stage, 55, t);
    content_divider_create(stage, 102, t);
    content_divider_create(stage, 149, t);
    for (uint8_t i = 0; i < 4; ++i) {
        s_focus_y[i] = 10 + i * 47;
        s_focus_components[i] = ui_glass_row_create(
            stage, 6, s_focus_y[i], 196, 42, labels[i], values[i], t);
    }
    focus_bind(lens, 4, 0);
}

static void morph_menu_refresh(bool animate)
{
    const ui_glass_theme_t *t = theme();
    for (uint8_t i = 0; i < 3; ++i) {
        uint32_t color = i == s_morph.item ? t->text : t->text_muted;
        set_label_color(s_morph.menu_rows[i], color);
    }
    if (!s_morph.highlight) return;
    int16_t target_y = (int16_t)(48 + s_morph.item * 44);
    if (!animate) {
        lv_obj_set_y(s_morph.highlight, target_y);
        return;
    }
    lv_anim_delete(&s_focus_motion, focus_lens_set);
    s_focus_motion = (focus_motion_t) {
        .lens = s_morph.highlight,
        .start_y = lv_obj_get_y(s_morph.highlight),
        .target_y = target_y,
    };
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &s_focus_motion);
    lv_anim_set_exec_cb(&animation, focus_lens_set);
    lv_anim_set_values(&animation, 0, UI_GLASS_MOTION_PROGRESS_MAX);
    lv_anim_set_duration(&animation, ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_FOCUS));
    lv_anim_start(&animation);
}

static uint8_t opacity_from_range(int32_t progress, int32_t start,
                                  int32_t end)
{
    if (progress <= start) return 0;
    if (progress >= end) return 255;
    return (uint8_t)((progress - start) * 255 / (end - start));
}

static void morph_set(void *value, int32_t progress)
{
    morph_view_t *morph = value;
    if (!morph || !morph->surface) return;
    int32_t eased = ui_glass_spring(progress);
    int32_t visual_eased = ui_glass_ease_out_cubic(progress);
    ui_glass_morph_frame_t frame = ui_glass_morph_interpolate(
        morph->from, morph->to, eased);
    lv_obj_set_pos(morph->surface, frame.x, frame.y);
    lv_obj_set_size(morph->surface, frame.width, frame.height);
    lv_obj_set_style_radius(morph->surface, frame.radius, 0);
    int32_t openness = morph->target_open
                           ? visual_eased
                           : UI_GLASS_MOTION_PROGRESS_MAX - visual_eased;
    morph->visual_depth = (uint8_t)(openness * 4 /
                                    UI_GLASS_MOTION_PROGRESS_MAX);
    reference_glass_apply(morph->surface, morph->base_tint,
                          frame.opacity, morph->visual_depth);
    if (morph->shadow) {
        int16_t expansion = morph->visual_depth;
        lv_obj_set_pos(morph->shadow,
                       frame.x - expansion,
                       frame.y + 2 + morph->visual_depth);
        lv_obj_set_size(morph->shadow,
                        frame.width + expansion * 2,
                        frame.height + expansion * 2);
        lv_obj_set_style_radius(morph->shadow,
                                frame.radius + expansion, 0);
        lv_obj_set_style_bg_opa(
            morph->shadow,
            (lv_opa_t)(30 + openness * 48 /
                       UI_GLASS_MOTION_PROGRESS_MAX), 0);
    }
    ui_glass_surface_set_glint(
        morph->surface,
        morph->target_open
            ? -256 + progress * 1536 / UI_GLASS_MOTION_PROGRESS_MAX
            : 1280 - progress * 1536 / UI_GLASS_MOTION_PROGRESS_MAX);

    uint8_t menu_opacity = morph->target_open
                               ? opacity_from_range(progress, 330, 760)
                               : (uint8_t)(255 -
                                   opacity_from_range(progress, 100, 500));
    uint8_t button_opacity = (uint8_t)(255 - menu_opacity);
    lv_obj_set_style_opa(morph->menu, menu_opacity, 0);
    lv_obj_set_style_opa(morph->button_label, button_opacity, 0);
    if (morph->dimmer) {
        // Keep only a slight atmospheric separation. Semantic content fades
        // independently below, so the wallpaper can remain optically active.
        lv_obj_set_style_bg_opa(
            morph->dimmer,
            (lv_opa_t)(openness * 36 / UI_GLASS_MOTION_PROGRESS_MAX), 0);
    }
    if (morph->context) {
        // The content is fully gone before the menu becomes readable and only
        // returns after the closing menu is nearly gone. A linear crossfade
        // put both titles near 50% at the midpoint and produced visible text
        // collisions even though the geometry itself was continuous.
        lv_obj_set_style_opa(
            morph->context,
            (lv_opa_t)(255 - opacity_from_range(
                openness, 80, 240)), 0);
    }
}

static void morph_completed(lv_anim_t *animation)
{
    morph_view_t *morph = lv_anim_get_user_data(animation);
    if (!morph) return;
    morph->open = morph->target_open;
    morph->animating = false;
    morph_set(morph, UI_GLASS_MOTION_PROGRESS_MAX);
}

static void morph_toggle(void)
{
    if (!s_morph.surface || s_morph.animating) return;
    ui_glass_morph_frame_t collapsed = {
        .x = 184, .y = 14, .width = 40, .height = 40,
        .radius = 20, .opacity = 148,
    };
    ui_glass_morph_frame_t expanded = {
        .x = 34, .y = 12, .width = 192, .height = 190,
        .radius = 28, .opacity = 142,
    };
    s_morph.target_open = !s_morph.open;
    s_morph.from = s_morph.open ? expanded : collapsed;
    s_morph.to = s_morph.open ? collapsed : expanded;
    s_morph.animating = true;

    uint16_t duration = ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_MORPH);
    if (duration == 0) {
        s_morph.open = s_morph.target_open;
        s_morph.animating = false;
        morph_set(&s_morph, UI_GLASS_MOTION_PROGRESS_MAX);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &s_morph);
    lv_anim_set_user_data(&animation, &s_morph);
    lv_anim_set_exec_cb(&animation, morph_set);
    lv_anim_set_values(&animation, 0, UI_GLASS_MOTION_PROGRESS_MAX);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_completed_cb(&animation, morph_completed);
    lv_anim_start(&animation);
}

static void build_overlays(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *scene = lv_obj_get_parent(root);
    lv_obj_t *content = content_layer_create(root, 14, 6, 212, 252, t);
    s_morph.context = content;
    solid_object(content, 8, 8, 196, 76, 20, 0x00101C, LV_OPA_30);
    text_at(content, "Now Playing", 16, 16,
            &lv_font_montserrat_14, t->text_muted);
    text_at(content, "Midnight Current", 16, 40,
            &lv_font_montserrat_20, t->text);
    s_player_state_label = text_at(
        content, s_player_playing ? "Playing  |  24 min"
                                  : "Paused  |  24 min",
        16, 66, &lv_font_montserrat_14, t->text_muted);

    lv_obj_t *art = solid_object(content, 14, 94, 184, 96, 26,
                                 0x0E4669, LV_OPA_COVER);
    lv_obj_t *orb = solid_object(art, 15, 14, 68, 68, LV_RADIUS_CIRCLE,
                                 t->accent, 88);
    lv_obj_t *audio = ui_glass_label(
        orb, LV_SYMBOL_AUDIO, &lv_font_montserrat_20, t->text);
    lv_obj_center(audio);
    static const uint8_t bar_heights[] = { 22, 48, 60, 34 };
    for (uint8_t i = 0; i < sizeof(bar_heights); ++i) {
        int height = bar_heights[i];
        solid_object(art, 108 + i * 14, 48 - height / 2,
                     5, height, 2, t->text, i == 2 ? 230 : 126);
    }
    text_at(content, LV_SYMBOL_BLUETOOTH "  Passport Speaker", 18, 207,
            &lv_font_montserrat_14, t->text);
    text_at(content, "Connected", 18, 229,
            &lv_font_montserrat_14, t->positive);

    s_morph.dimmer = solid_object(scene, 0, 0,
                                  240, LIQUID_GLASS_COMPOSITOR_HEIGHT,
                                  0, 0x00040A, LV_OPA_TRANSP);
    lv_obj_t *overlay = plain_object(
        scene, 0, SHOWCASE_SCENE_Y,
        LIQUID_GLASS_COMPOSITOR_WIDTH, SHOWCASE_SCENE_HEIGHT);
    s_morph.shadow = solid_object(overlay, 184, 17, 40, 40, 20,
                                  0x01070D, 30);
    s_morph.surface = reference_glass_create(
        overlay, 184, 14, 40, 40, 20, 148, 0, t, &s_morph.base_tint);
    s_morph.button_label = ui_glass_label(
        s_morph.surface, LV_SYMBOL_LIST, &lv_font_montserrat_14, t->text);
    lv_obj_center(s_morph.button_label);

    s_morph.menu = plain_object(s_morph.surface, 0, 0, 192, 190);
    lv_obj_set_style_opa(s_morph.menu, LV_OPA_TRANSP, 0);
    text_at(s_morph.menu, "Quick Actions", 18, 16,
            &lv_font_montserrat_20, t->text);
    s_morph.highlight = solid_object(
        s_morph.menu, 8, 48, 176, 38, 17, t->accent, 38);
    static const char *const rows[] = {
        LV_SYMBOL_BLUETOOTH "   Connect",
        LV_SYMBOL_AUDIO "   Sound",
        LV_SYMBOL_POWER "   Sleep",
    };
    for (uint8_t i = 0; i < 3; ++i) {
        s_morph.menu_rows[i] = text_at(
            s_morph.menu, rows[i], 20, 58 + i * 44,
            &lv_font_montserrat_14, t->text);
    }
    s_morph.item = 0;
    s_morph.visual_depth = 0;
    morph_menu_refresh(false);
}

static void navigation_content_update(uint8_t index)
{
    static const char *const titles[] = {
        "Good evening", "Midnight Current", "Passport Linked",
    };
    static const char *const subtitles[] = {
        "Everything is ready", "Ambient focus mix", "Bluetooth  |  Stable",
    };
    static const char *const symbols[] = {
        LV_SYMBOL_HOME, LV_SYMBOL_PLAY, LV_SYMBOL_BLUETOOTH,
    };
    static const uint32_t colors[] = { 0x3B93C5, 0x5159B8, 0x167D69 };
    if (index > 2) index = 0;

    lv_label_set_text(s_navigation.title, titles[index]);
    lv_label_set_text(s_navigation.subtitle, subtitles[index]);
    lv_obj_set_style_bg_color(s_navigation.hero,
                              lv_color_hex(colors[index]), 0);
    lv_label_set_text(s_navigation.symbol, symbols[index]);
    lv_label_set_text(s_navigation.state_label,
                      index == 0 ? "READY" : index == 1 ? "PLAYING" : "ONLINE");
    lv_label_set_text(s_navigation.value_label,
                      index == 0 ? "100%" : index == 1 ? "42%" : "-48 dBm");
}

static void navigation_content_create(lv_obj_t *panel,
                                      const ui_glass_theme_t *t)
{
    s_navigation.content = plain_object(panel, 0, 0, 212, 252);
    solid_object(s_navigation.content, 8, 8, 196, 60, 18,
                 0x00101C, LV_OPA_20);
    s_navigation.title = text_at(s_navigation.content, "", 16, 18,
                                 &lv_font_montserrat_20, t->text);
    s_navigation.subtitle = text_at(s_navigation.content, "", 16, 47,
                                    &lv_font_montserrat_14, t->text_muted);

    s_navigation.hero = solid_object(s_navigation.content, 14, 76, 184, 110,
                                     28, 0x3B93C5, LV_OPA_COVER);
    lv_obj_t *orb = solid_object(s_navigation.hero, 16, 20, 70, 70,
                                 LV_RADIUS_CIRCLE, t->text, 34);
    s_navigation.symbol = ui_glass_label(
        orb, "", &lv_font_montserrat_20, t->text);
    lv_obj_center(s_navigation.symbol);
    s_navigation.state_label = text_at(
        s_navigation.hero, "", 104, 31, &lv_font_montserrat_14, t->text);
    s_navigation.value_label = text_at(
        s_navigation.hero, "", 104, 56, &lv_font_montserrat_14, t->text_muted);

    navigation_content_update(s_navigation_index);
}

static void navigation_refresh_tabs(uint8_t selected)
{
    const ui_glass_theme_t *t = theme();
    for (uint8_t i = 0; i < 3; ++i) {
        uint32_t color = i == selected ? t->text : t->text_muted;
        set_label_color(s_navigation.tab_items[i], color);
    }
}

static void navigation_motion_set(void *value, int32_t progress)
{
    navigation_view_t *navigation = value;
    if (!navigation || !navigation->selection) return;
    int32_t spring = ui_glass_spring(progress);
    int16_t selection_x = (int16_t)ui_glass_interpolate(
        navigation->start_x, navigation->target_x, spring);
    lv_obj_set_x(navigation->selection, selection_x);
    if (navigation->selection_shadow) {
        lv_obj_set_x(navigation->selection_shadow, selection_x + 1);
    }
    ui_glass_surface_set_glint(
        navigation->dock,
        -256 + progress * 1536 / UI_GLASS_MOTION_PROGRESS_MAX);

    if (navigation->content) {
        if (progress < 500) {
            int32_t phase = progress * UI_GLASS_MOTION_PROGRESS_MAX / 500;
            lv_obj_set_x(navigation->content,
                         -navigation->direction * 16 * phase /
                             UI_GLASS_MOTION_PROGRESS_MAX);
            lv_obj_set_style_opa(
                navigation->content,
                (lv_opa_t)(255 - 255 * phase /
                           UI_GLASS_MOTION_PROGRESS_MAX), 0);
        } else {
            if (!navigation->content_swapped) {
                navigation_content_update(navigation->target_index);
                navigation->content_swapped = true;
            }
            int32_t phase = (progress - 500) * UI_GLASS_MOTION_PROGRESS_MAX /
                            (UI_GLASS_MOTION_PROGRESS_MAX - 500);
            if (phase > UI_GLASS_MOTION_PROGRESS_MAX) {
                phase = UI_GLASS_MOTION_PROGRESS_MAX;
            }
            lv_obj_set_x(navigation->content,
                         navigation->direction * 16 *
                             (UI_GLASS_MOTION_PROGRESS_MAX - phase) /
                             UI_GLASS_MOTION_PROGRESS_MAX);
            lv_obj_set_style_opa(
                navigation->content,
                (lv_opa_t)(255 * phase / UI_GLASS_MOTION_PROGRESS_MAX), 0);
        }
    }
}

static void navigation_motion_completed(lv_anim_t *animation)
{
    navigation_view_t *navigation = lv_anim_get_user_data(animation);
    if (!navigation) return;
    if (!navigation->content_swapped) {
        navigation_content_update(navigation->target_index);
    }
    navigation->animating = false;
    if (navigation->content) {
        lv_obj_set_x(navigation->content, 0);
        lv_obj_set_style_opa(navigation->content, LV_OPA_COVER, 0);
    }
    ui_glass_surface_set_glint(navigation->dock, UI_GLASS_GLINT_HIDDEN);
}

static void navigation_select(int8_t direction)
{
    if (!s_navigation.panel || s_navigation.animating) return;
    uint8_t next = (uint8_t)((s_navigation_index +
                              (direction < 0 ? 2 : 1)) % 3);
    s_navigation.animating = true;
    s_navigation.direction = direction < 0 ? -1 : 1;
    s_navigation.start_x = lv_obj_get_x(s_navigation.selection);
    s_navigation.target_x = (int16_t)(6 + next * 64);
    s_navigation.target_index = next;
    s_navigation.content_swapped = false;
    s_navigation_index = next;
    navigation_refresh_tabs(next);

    uint16_t duration = ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_MORPH);
    if (duration == 0) {
        navigation_motion_set(&s_navigation, UI_GLASS_MOTION_PROGRESS_MAX);
        s_navigation.animating = false;
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &s_navigation);
    lv_anim_set_user_data(&animation, &s_navigation);
    lv_anim_set_exec_cb(&animation, navigation_motion_set);
    lv_anim_set_values(&animation, 0, UI_GLASS_MOTION_PROGRESS_MAX);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_completed_cb(&animation, navigation_motion_completed);
    lv_anim_start(&animation);
}

static void build_navigation(lv_obj_t *root)
{
    static const char *const tabs[] = {
        LV_SYMBOL_HOME "\nHome",
        LV_SYMBOL_PLAY "\nPlay",
        LV_SYMBOL_BLUETOOTH "\nLink",
    };
    const ui_glass_theme_t *t = theme();
    s_navigation.panel = content_layer_create(root, 14, 6, 212, 252, t);
    navigation_content_create(s_navigation.panel, t);
    s_navigation.dock_shadow = solid_object(
        root, 16, 212, 208, 58, 30, 0x01070D, 44);
    s_navigation.dock = reference_glass_create(
        root, 18, 208, 204, 56, 28, 144, 1, t, NULL);
    s_navigation.selection_shadow = solid_object(
        s_navigation.dock, 7 + s_navigation_index * 64, 7, 64, 46,
        23, 0x01070D, 28);
    s_navigation.selection = solid_object(
        s_navigation.dock, 6 + s_navigation_index * 64, 5, 64, 46,
        23, t->accent, 42);
    for (uint8_t i = 0; i < 3; ++i) {
        s_navigation.tab_items[i] = text_at(
            s_navigation.dock, tabs[i], 6 + i * 64, 11,
            &lv_font_montserrat_14, t->text_muted);
        lv_obj_set_width(s_navigation.tab_items[i], 64);
        lv_obj_set_style_text_align(s_navigation.tab_items[i],
                                    LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(s_navigation.tab_items[i], -2, 0);
    }
    navigation_refresh_tabs(s_navigation_index);
}

static lv_obj_t *feedback_chip(lv_obj_t *parent, int x, int y, int width,
                               const char *label, uint32_t color,
                               lv_obj_t **label_out)
{
    lv_obj_t *chip = solid_object(parent, x, y, width, 28,
                                  LV_RADIUS_CIRCLE, color, LV_OPA_30);
    lv_obj_t *text = centered_label(
        chip, label, &lv_font_montserrat_14, color);
    if (label_out) *label_out = text;
    return chip;
}

static void feedback_refresh(bool animate)
{
    static const char *const statuses[] = {
        "Syncing", "Paused", "Failed",
    };
    static const char *const titles[] = {
        "Saving offline", "Sync paused", "Can't sync library",
    };
    static const char *const details[] = {
        "Midnight Current", "Ready to resume", "Press OK to retry",
    };
    static const char *const toasts[] = {
        "Available offline", "Waiting for network", "Retry ready",
    };
    static const uint8_t progress[] = { 64, 64, 38 };
    const ui_glass_theme_t *t = theme();
    uint8_t state = s_feedback_state % 3u;
    uint32_t color = state == 0 ? t->accent
                               : state == 1 ? t->warning : t->danger;
    if (!s_feedback.status_chip || !s_feedback.status_label ||
        !s_feedback.alert || !s_feedback.alert_title ||
        !s_feedback.alert_detail || !s_feedback.progress.indicator ||
        !s_feedback.progress.value || !s_feedback.toast_label) {
        return;
    }

    lv_obj_set_style_bg_color(s_feedback.status_chip,
                              lv_color_hex(color), 0);
    lv_label_set_text(s_feedback.status_label, statuses[state]);
    set_label_color(s_feedback.status_label, color);
    lv_obj_set_style_bg_color(s_feedback.alert, lv_color_hex(color), 0);
    lv_label_set_text(s_feedback.alert_title, titles[state]);
    set_label_color(s_feedback.alert_title, color);
    lv_label_set_text(s_feedback.alert_detail, details[state]);
    lv_label_set_text_fmt(s_feedback.progress.value, "%u%%",
                          progress[state]);
    lv_obj_set_style_bg_color(s_feedback.progress.indicator,
                              lv_color_hex(color), 0);
    if (animate) {
        animate_width(s_feedback.progress.indicator,
                      168 * progress[state] / 100);
    } else {
        lv_obj_set_width(s_feedback.progress.indicator,
                         168 * progress[state] / 100);
    }
    lv_label_set_text(s_feedback.toast_label, toasts[state]);
    lv_obj_align(s_feedback.toast_label, LV_ALIGN_CENTER, 0, -1);
}

static void build_feedback(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = showcase_content_stage_create(root, t);
    feedback_chip(stage, 14, 10, 82, "Online", t->positive, NULL);
    s_feedback.status_chip = feedback_chip(
        stage, 108, 10, 82, "", t->accent, &s_feedback.status_label);

    s_feedback.alert = solid_object(stage, 14, 50, 180, 52, 14,
                                    t->accent, LV_OPA_20);
    s_feedback.alert_title = text_at(
        s_feedback.alert, "", 14, 8, &lv_font_montserrat_14, t->accent);
    s_feedback.alert_detail = text_at(
        s_feedback.alert, "", 14, 29,
        &lv_font_montserrat_14, t->text_muted);

    s_feedback.progress = showcase_progress_create(
        stage, 108, "Offline mix", 64, t);
    lv_obj_t *toast = ui_glass_platter_create(stage, 26, 158, 156, 34, t);
    s_feedback.toast_label = centered_label(
        toast, "", &lv_font_montserrat_14, t->text);
    feedback_refresh(false);
}

static void build_states(lv_obj_t *root)
{
    static const char *const labels[] = {
        "Standard", "High contrast", "Reduce glass", "Reduce motion",
    };
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = showcase_content_stage_create(root, t);
    lv_obj_t *lens = ui_glass_focus_lens_create(stage, 6, 10, 196, 42, t);
    content_divider_create(stage, 55, t);
    content_divider_create(stage, 102, t);
    content_divider_create(stage, 149, t);
    for (uint8_t i = 0; i < UI_GLASS_MODE_COUNT; ++i) {
        s_focus_y[i] = 10 + i * 47;
        s_focus_components[i] = ui_glass_row_create(
            stage, 6, s_focus_y[i], 196, 42, labels[i],
            i == s_runtime.mode ? "ON" : "", t);
    }
    focus_bind(lens, UI_GLASS_MODE_COUNT, (uint8_t)s_runtime.mode);
}

static lv_obj_t *scene_create(void)
{
    lv_obj_t *scene = plain_object(s_runtime.screen, 0, 0,
                                   LIQUID_GLASS_COMPOSITOR_WIDTH,
                                   LIQUID_GLASS_COMPOSITOR_HEIGHT);
    return scene;
}

static void scene_build(showcase_page_t page, lv_obj_t *root)
{
    lv_obj_t *content = plain_object(
        root, 0, SHOWCASE_SCENE_Y,
        LIQUID_GLASS_COMPOSITOR_WIDTH, SHOWCASE_SCENE_HEIGHT);
    switch (page) {
    case SHOWCASE_BUTTONS:       build_buttons(content); break;
    case SHOWCASE_SELECTION:     build_selection(content); break;
    case SHOWCASE_ADJUSTMENTS:   build_adjustments(content); break;
    case SHOWCASE_LISTS:         build_lists(content); break;
    case SHOWCASE_OVERLAYS:      build_overlays(content); break;
    case SHOWCASE_NAVIGATION:    build_navigation(content); break;
    case SHOWCASE_FEEDBACK:      build_feedback(content); break;
    case SHOWCASE_STATES:        build_states(content); break;
    default:                     build_buttons(content); break;
    }
}

static void shell_refresh(void)
{
    const ui_glass_theme_t *t = theme();
    uint8_t position = page_position(s_page);
    lv_label_set_text_fmt(s_header_title, "%u/8  %s",
                          (unsigned)position + 1u, PAGE_TITLES[s_page]);
    set_label_color(s_header_title, t->text);
    set_label_color(s_battery_label, t->text);
    if (s_presented_battery_soc != s_battery_soc) {
        if (s_battery_soc >= 0) {
            lv_label_set_text_fmt(s_battery_label, "%d%%", s_battery_soc);
        } else {
            lv_label_set_text(s_battery_label, "--%");
        }
        s_presented_battery_soc = s_battery_soc;
    }
    lv_label_set_text_fmt(
        s_footer_label, "UP NEXT  %s  %s",
        PAGE_SECONDARY_ACTION[s_page], PAGE_PRIMARY_ACTION[s_page]);
    set_label_color(s_footer_label, t->text_muted);
    ui_glass_surface_set_tint(s_footer, t->control_tint,
                              t->control_opacity);
    ui_glass_surface_set_material(s_footer, t->control_material);
    ui_glass_surface_set_edge_strength(s_footer, t->focus_edge_strength);
    if (s_page == SHOWCASE_OVERLAYS || s_page == SHOWCASE_NAVIGATION) {
        lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
    }
    // Scenes are replaced throughout the reel, while the header and footer are
    // persistent chrome. Keeping title and battery under one parent makes
    // their clipping and Z order atomic across a translating page scene.
    lv_obj_move_foreground(s_header_chrome);
    lv_obj_move_foreground(s_footer);
    // Text changes invalidate intrinsic label sizes. Resolve aligned chrome in
    // the same frame so the first post-transition render cannot use the old
    // footer width or temporarily omit the updated battery glyphs.
    lv_obj_update_layout(s_runtime.screen);
}

static void page_transition_set(void *value, int32_t progress)
{
    page_transition_t *transition = value;
    if (!transition || !transition->incoming || !transition->outgoing) return;
    int32_t eased = ui_glass_ease_in_out_cubic(progress);
    int32_t travel = ui_glass_interpolate(
        0, LIQUID_GLASS_COMPOSITOR_WIDTH, eased);
    lv_obj_set_x(transition->outgoing, -transition->direction * travel);
    lv_obj_set_x(transition->incoming,
                 transition->direction *
                 (LIQUID_GLASS_COMPOSITOR_WIDTH - travel));
    lv_obj_set_style_opa(transition->outgoing, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(transition->incoming, LV_OPA_COVER, 0);
}

static void scene_timer_finish(lv_timer_t *timer)
{
    if (!timer) return;
    lv_timer_delete(timer);
    if (s_scene_timer == timer) s_scene_timer = NULL;
}

static void scene_showcase_tick(lv_timer_t *timer)
{
    if (s_transitioning) return;
    s_scene_step++;
    switch (s_page) {
    case SHOWCASE_BUTTONS:
        if (s_scene_step == 1) focused_action();
        else if (s_scene_step == 2 || s_scene_step == 4) focus_move(1);
        else if (s_scene_step == 3 || s_scene_step == 5) focused_action();
        else scene_timer_finish(timer);
        break;
    case SHOWCASE_SELECTION:
        if (s_scene_step == 1) focused_action();
        else if (s_scene_step == 2 || s_scene_step == 4) focus_move(1);
        else if (s_scene_step == 3) focused_action();
        else if (s_scene_step == 5) press_focused_component();
        else scene_timer_finish(timer);
        break;
    case SHOWCASE_ADJUSTMENTS:
        if (s_scene_step == 1) focused_action();
        else if (s_scene_step == 2 || s_scene_step == 4) focus_move(1);
        else if (s_scene_step == 3 || s_scene_step == 5) focused_action();
        else scene_timer_finish(timer);
        break;
    case SHOWCASE_LISTS:
        if (s_scene_step == 1 || s_scene_step == 2 || s_scene_step == 4) {
            focus_move(1);
        } else if (s_scene_step == 3 || s_scene_step == 5) {
            focused_action();
        } else {
            scene_timer_finish(timer);
        }
        break;
    case SHOWCASE_OVERLAYS:
        if (s_scene_step == 1 || s_scene_step == 4) {
            morph_toggle();
        } else if (s_scene_step == 2 || s_scene_step == 3) {
            s_morph.item = (s_morph.item + 1u) % 3u;
            morph_menu_refresh(true);
        } else if (s_scene_step == 5) {
            focused_action();
        } else {
            scene_timer_finish(timer);
        }
        break;
    case SHOWCASE_NAVIGATION:
        if (s_scene_step <= 3) navigation_select(1);
        else scene_timer_finish(timer);
        break;
    case SHOWCASE_FEEDBACK:
        if (s_scene_step <= 3) focused_action();
        else scene_timer_finish(timer);
        break;
    case SHOWCASE_STATES:
        if (s_scene_step == 1) focus_move(1);
        else if (s_scene_step == 2) focused_action();
        else scene_timer_finish(timer);
        break;
    default:
        scene_timer_finish(timer);
        break;
    }
}

static void start_scene_showcase(void)
{
    if (s_scene_timer) {
        lv_timer_delete(s_scene_timer);
        s_scene_timer = NULL;
    }
    switch (s_page) {
    case SHOWCASE_BUTTONS:
    case SHOWCASE_SELECTION:
    case SHOWCASE_ADJUSTMENTS:
    case SHOWCASE_LISTS:
    case SHOWCASE_OVERLAYS:
    case SHOWCASE_NAVIGATION:
    case SHOWCASE_FEEDBACK:
    case SHOWCASE_STATES:
        s_scene_step = 0;
        s_scene_timer = lv_timer_create(
            scene_showcase_tick, SHOWCASE_SCENE_STEP_MS, NULL);
        break;
    default:
        break;
    }
}

static void page_transition_completed(lv_anim_t *animation)
{
    page_transition_t *transition = lv_anim_get_user_data(animation);
    if (!transition) return;
    if (transition->outgoing) lv_obj_delete(transition->outgoing);
    if (transition->incoming) {
        lv_obj_set_x(transition->incoming, 0);
        lv_obj_set_style_opa(transition->incoming, LV_OPA_COVER, 0);
    }
    transition->outgoing = NULL;
    transition->incoming = NULL;
    s_transitioning = false;
    shell_refresh();
    start_scene_showcase();
}

static void show_page(showcase_page_t page, int8_t direction, bool animate)
{
    if (page >= SHOWCASE_PAGE_COUNT || s_transitioning) return;
    stop_scene_activity();
    lv_obj_t *old = s_scene;
    lv_obj_t *incoming = scene_create();
    s_page = page;
    s_scene = incoming;
    scene_build(page, incoming);

    // The incoming scene is created after the persistent chrome and would
    // otherwise sit above it until the transition-complete callback runs.
    // Keep status and command chrome stable for every intermediate frame.
    lv_obj_move_foreground(s_header_chrome);
    lv_obj_move_foreground(s_footer);
    // Player and Home own the lower safe area. Hide the previous page footer
    // before either destination begins sliding in so scene copy cannot cross
    // through a stale command capsule during the transition.
    if (page == SHOWCASE_OVERLAYS || page == SHOWCASE_NAVIGATION) {
        lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
    }

    uint16_t duration = ui_glass_motion_duration(
        s_runtime.mode, UI_GLASS_MOTION_PAGE);
    if (!animate || !old || duration == 0) {
        if (old) lv_obj_delete(old);
        lv_obj_set_x(incoming, 0);
        lv_obj_set_style_opa(incoming, LV_OPA_COVER, 0);
        shell_refresh();
        // Reduced Motion makes page changes immediate, but state-only scene
        // demonstrations (toggle, feedback, playback) must keep running.
        if (animate && old) start_scene_showcase();
        return;
    }

    s_transitioning = true;
    s_page_motion = (page_transition_t) {
        .outgoing = old,
        .incoming = incoming,
        .direction = direction < 0 ? -1 : 1,
    };
    page_transition_set(&s_page_motion, 0);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &s_page_motion);
    lv_anim_set_user_data(&animation, &s_page_motion);
    lv_anim_set_exec_cb(&animation, page_transition_set);
    lv_anim_set_values(&animation, 0, UI_GLASS_MOTION_PROGRESS_MAX);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_completed_cb(&animation, page_transition_completed);
    lv_anim_start(&animation);
}

static void navigate_showcase_page(int8_t direction)
{
    uint8_t index = page_position(s_page);
    index = (uint8_t)((index +
                       (direction < 0 ? SHOWCASE_PAGE_COUNT - 1 : 1)) %
                      SHOWCASE_PAGE_COUNT);
    show_page(PAGE_ORDER[index], direction, true);
}

static void rebuild_page(void)
{
    show_page(s_page, 1, false);
}

static void tour_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_transitioning) return;
    s_tour_steps = (uint8_t)((s_tour_steps + 1u) % SHOWCASE_PAGE_COUNT);
    navigate_showcase_page(1);
}

static void focused_action(void)
{
    switch (s_page) {
    case SHOWCASE_BUTTONS:
        press_focused_component();
        break;
    case SHOWCASE_SELECTION:
        if (s_focus.index == 0) {
            segment_select((uint8_t)((s_segment_index + 1u) % 3u), true);
        } else if (s_focus.index == 1) {
            s_control_toggle = !s_control_toggle;
            ui_glass_toggle_set_animated(
                &s_focus_components[1], s_control_toggle, theme(),
                ui_glass_motion_duration(s_runtime.mode,
                                         UI_GLASS_MOTION_FOCUS));
        } else if (s_focus.index == 2) {
            s_selection_check = !s_selection_check;
            rebuild_page();
        } else {
            s_selection_radio = !s_selection_radio;
            rebuild_page();
        }
        break;
    case SHOWCASE_ADJUSTMENTS:
        if (s_focus.index == 0) {
            s_control_slider = s_control_slider >= 92
                                   ? 20 : s_control_slider + 18;
            ui_glass_slider_set_animated(
                &s_focus_components[0], s_control_slider, theme(),
                ui_glass_motion_duration(s_runtime.mode,
                                         UI_GLASS_MOTION_FOCUS));
        } else if (s_focus.index == 1) {
            s_stepper_value = s_stepper_value >= 5
                                  ? 1 : s_stepper_value + 1;
            lv_label_set_text_fmt(s_focus_components[1].value, "%u",
                                  s_stepper_value);
        } else {
            s_device_volume = s_device_volume >= 90
                                  ? 30 : s_device_volume + 20;
            lv_label_set_text_fmt(s_focus_components[2].value, "%u%%",
                                  s_device_volume);
            animate_width(s_focus_components[2].indicator,
                          168 * s_device_volume / 100);
        }
        break;
    case SHOWCASE_LISTS:
        press_focused_component();
        if (s_focus.index < s_focus_count &&
            s_focus_components[s_focus.index].value) {
            lv_label_set_text(s_focus_components[s_focus.index].value,
                              "OPEN");
        }
        break;
    case SHOWCASE_OVERLAYS:
        if (s_morph.open) {
            morph_toggle();
        } else {
            s_player_playing = !s_player_playing;
            if (s_player_state_label) {
                lv_label_set_text(
                    s_player_state_label,
                    s_player_playing ? "Playing  |  24 min"
                                     : "Paused  |  24 min");
            }
        }
        break;
    case SHOWCASE_NAVIGATION:
        navigation_select(1);
        break;
    case SHOWCASE_FEEDBACK:
        s_feedback_state = (s_feedback_state + 1u) % 3u;
        feedback_refresh(true);
        break;
    case SHOWCASE_STATES:
        ui_glass_runtime_set_mode(
            &s_runtime, (ui_glass_mode_t)s_focus.index);
        rebuild_page();
        break;
    default:
        break;
    }
}

void demo_glass_system_set_battery(int soc)
{
    s_battery_soc = soc;
}

void demo_glass_system_enter(void)
{
    s_page = SHOWCASE_OVERLAYS;
    s_transitioning = false;
    s_tour_steps = 0;
    s_presented_battery_soc = -2;
    if (!ui_glass_runtime_init(&s_runtime, UI_GLASS_MODE_STANDARD,
                               UI_GLASS_QUALITY_FULL)) {
        return;
    }
    const ui_glass_theme_t *t = theme();
    s_header_chrome = plain_object(
        s_runtime.screen, 0, 0, LIQUID_GLASS_COMPOSITOR_WIDTH,
        SHOWCASE_SCENE_Y);
    s_header_title = text_at(s_header_chrome, PAGE_TITLES[s_page],
                             16, 12, &lv_font_montserrat_20, t->text);
    s_battery_label = text_at(s_header_chrome, "--%", 188, 13,
                              &lv_font_montserrat_14, t->text);
    // Fixed status geometry avoids an initial auto-size/wrap pass dropping a
    // 3-digit percentage at the exact moment a new scene becomes visible.
    lv_obj_set_size(s_battery_label, 36, 20);
    lv_obj_set_style_text_align(s_battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    s_footer = ui_glass_platter_create(
        s_runtime.screen, 14, 272, 212, 36, t);
    s_footer_label = ui_glass_label(
        s_footer, "UP NEXT  MOVE  USE",
        &lv_font_montserrat_14, t->text_muted);
    // Footer copy changes by page, so give it one stable text slot instead of
    // re-centering an auto-sized label after every string replacement.
    lv_obj_set_pos(s_footer_label, 0, 7);
    lv_obj_set_size(s_footer_label, 212, 20);
    lv_obj_set_style_text_align(s_footer_label, LV_TEXT_ALIGN_CENTER, 0);

    s_scene = scene_create();
    scene_build(s_page, s_scene);
    shell_refresh();
    lv_screen_load(s_runtime.screen);
    start_scene_showcase();
    s_tour_timer = lv_timer_create(tour_tick, SHOWCASE_TOUR_PERIOD_MS, NULL);
}

void demo_glass_system_exit(void)
{
    stop_tour();
    stop_scene_activity();
    lv_anim_delete(&s_page_motion, page_transition_set);
    s_transitioning = false;
    ui_glass_runtime_deinit(&s_runtime);
    s_scene = NULL;
    s_header_chrome = NULL;
    s_header_title = NULL;
    s_battery_label = NULL;
    s_footer = NULL;
    s_footer_label = NULL;
    s_presented_battery_soc = -2;
    memset(&s_page_motion, 0, sizeof(s_page_motion));
}

void demo_glass_system_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    stop_tour();
    stop_scene_timer();
    if (s_transitioning) return;

    if (event == BSP_BTN_DOUBLE) {
        if (button == BSP_BTN_UP) navigate_showcase_page(-1);
        return;
    }
    if (event != BSP_BTN_CLICK) return;

    // UP is the single, invariant page key. DOWN and OK retain contextual
    // meaning inside each scene, so the interaction model is learnable without
    // a help screen.
    if (button == BSP_BTN_UP) {
        navigate_showcase_page(1);
        return;
    }

    if (s_page == SHOWCASE_OVERLAYS && s_morph.open) {
        if (button == BSP_BTN_DOWN) {
            s_morph.item = (s_morph.item + 1u) % 3u;
            morph_menu_refresh(true);
        } else if (button == BSP_BTN_OK) {
            morph_toggle();
        }
        return;
    }

    if (s_page == SHOWCASE_OVERLAYS && button == BSP_BTN_DOWN) {
        morph_toggle();
        return;
    }

    if (s_page == SHOWCASE_NAVIGATION && button == BSP_BTN_DOWN) {
        navigation_select(1);
    } else if (s_page == SHOWCASE_FEEDBACK && button == BSP_BTN_DOWN) {
        s_feedback_state = (uint8_t)((s_feedback_state + 2u) % 3u);
        feedback_refresh(true);
    } else if (button == BSP_BTN_DOWN) {
        focus_move(1);
    } else if (button == BSP_BTN_OK) {
        focused_action();
    }
}
