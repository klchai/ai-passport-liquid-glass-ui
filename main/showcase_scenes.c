// main/showcase_scenes.c —— 十页应用：八个 Liquid Glass 展示场景 + Kaboo + Claude。
//
// 这是应用的全部 UI。八个展示场景演示交互原语；Kaboo / Claude 两页显示由
// Mac 经 BLE 推来的实时数据（见 usage_link.c）。十页共用一个 runtime、一块
// 屏幕、一套 header/footer chrome、一个 200ms 主定时器和同一个焦点/转场系统。
//
// 按键约定（十页统一，沿用展示场景的原则）：
//   UP        下一页（唯一的全局翻页键）
//   UP 双击    上一页
//   DOWN      页内：移动焦点 / 切换页内状态（Kaboo 翻卡片）
//   OK        执行当前焦点动作
#include "dashboard.h"

#include "ui_glass.h"
#include "ui_glass_focus.h"
#include "ui_glass_motion.h"
#include "ui_glass_runtime.h"
#include "ui_glass_widgets.h"
#include "usage_link.h"

#include "esp_timer.h"
#include "lvgl.h"

#include <inttypes.h>
#include <stdio.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SHOWCASE_PAGE_COUNT      10
#define SHOWCASE_SCENE_Y         44
#define SHOWCASE_SCENE_HEIGHT   276
// One master tick drives everything. 200ms is the GCD of every period below,
// so independent counters never drift against each other.
#define SHOWCASE_TICK_MS         200
// The unattended reel is meant to be watched, not benchmarked. Seven seconds
// leaves time to read the scene after its signature interactions have played.
#define SHOWCASE_TOUR_PERIOD_MS 7000
#define SHOWCASE_SCENE_STEP_MS  1000
#define SHOWCASE_MAX_FOCUS        6

// Kaboo 卡片轮换：8 秒够读完一个大数字连同费用（4 秒在真机上被反馈为翻太快）。
// 用户手动翻卡后暂停自动轮换一段时间，避免刚看清就被翻走。
#define KABOO_CARD_COUNT          3
#define CARD_ROTATE_MS         8000
#define CARD_MANUAL_HOLD_MS   10000
// 数据源 TTL：超过这个时长就不再声称数字是"当前"值。
#define SOURCE_TTL_SECONDS      900

LV_FONT_DECLARE(font_digits_44);

// 前八个是展示场景，保持原枚举顺序不变（页内状态表按枚举索引）；后两个是
// 实时数据页。is_data_page() 依赖这个排列。
typedef enum {
    SHOWCASE_BUTTONS = 0,
    SHOWCASE_SELECTION,
    SHOWCASE_ADJUSTMENTS,
    SHOWCASE_LISTS,
    SHOWCASE_OVERLAYS,
    SHOWCASE_NAVIGATION,
    SHOWCASE_FEEDBACK,
    SHOWCASE_STATES,
    PAGE_KABOO,
    PAGE_CLAUDE,
} showcase_page_t;

static bool is_data_page(showcase_page_t page)
{
    return page >= PAGE_KABOO;
}

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
    "Kaboo",
    "Claude",
};

// The public reel starts with the two strongest content-first scenes, then
// unfolds the interaction patterns that power them. Stable enum identities let
// component state survive a reordered presentation sequence. The two live-data
// pages sit at the back of the book so the demonstration reel stays intact.
//
// Footer width was measured against this order: the widest neighbour pair is
// "Activity / Appearance" on Moments at 187px inside the 212px platter.
// "Appearance" never sits beside "Moments", so the two longest names never
// share a row. Reordering requires re-measuring.
static const showcase_page_t PAGE_ORDER[SHOWCASE_PAGE_COUNT] = {
    SHOWCASE_OVERLAYS,
    SHOWCASE_NAVIGATION,
    SHOWCASE_SELECTION,
    SHOWCASE_ADJUSTMENTS,
    SHOWCASE_LISTS,
    SHOWCASE_FEEDBACK,
    SHOWCASE_BUTTONS,
    SHOWCASE_STATES,
    PAGE_KABOO,
    PAGE_CLAUDE,
};

static ui_glass_runtime_t s_runtime;
static lv_obj_t *s_scene;
static lv_obj_t *s_header_chrome;
static lv_obj_t *s_header_title;
static lv_obj_t *s_link_dot;        // BLE 链路指示：绿=已连接，灰=断开
static lv_obj_t *s_battery_label;   // 右上角电量，规范默认要求显示
static int s_battery_soc = -1;      // app_main 开机读一次；-1 = 不可用
static lv_obj_t *s_footer;
static lv_obj_t *s_footer_label;
// 单一主定时器。原本的巡航 timer 与场景步进 timer 都并入它的计数器，
// 消除了翻页时创建/销毁 timer 的竞争窗口。
static lv_timer_t *s_tick_timer;
static uint32_t s_tour_elapsed_ms;
static uint32_t s_step_elapsed_ms;
static bool s_tour_killed;          // 任何按键永久停止无人巡航
static bool s_scene_done;           // 当前场景的步进演示已跑完
static page_transition_t s_page_motion;

// ---- 实时数据页（Kaboo / Claude）状态 ----

static struct {
    lv_obj_t *label;      // 窗口名（TODAY / 7 DAYS / 30 DAYS）
    lv_obj_t *value;      // 大字 token 数
    lv_obj_t *cost;
    lv_obj_t *model;      // 模型 chip 文字
    lv_obj_t *dots[KABOO_CARD_COUNT];
    lv_obj_t *note;
} s_kaboo_view;

static struct {
    lv_obj_t *five_value;
    lv_obj_t *five_bar;
    lv_obj_t *five_reset;
    lv_obj_t *seven_value;
    lv_obj_t *seven_bar;
    lv_obj_t *seven_reset;
    lv_obj_t *note;
} s_claude_view;

static uint8_t  s_kaboo_card;
static uint32_t s_card_elapsed_ms;
static uint32_t s_card_hold_ms;
static focus_motion_t s_focus_motion;
static morph_view_t s_morph;
static showcase_page_t s_page;
static bool s_transitioning;
static uint8_t s_scene_step;
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
static void link_dot_refresh(void);

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

// 两个"停止"现在只是翻标志：真正的 timer 是常驻的主定时器，不再随页创建销毁。
static void stop_tour(void)
{
    s_tour_killed = true;
}

static void stop_scene_timer(void)
{
    s_scene_done = true;
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
    // 数据页的 view 指针随 scene 一起失效。refresh_data_page 靠 is_data_page
    // 门禁已经不会碰它们，清零是第二道保险，让任何越过门禁的路径都撞 NULL 而不是野指针。
    memset(&s_kaboo_view, 0, sizeof(s_kaboo_view));
    memset(&s_claude_view, 0, sizeof(s_claude_view));
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
    // 两行状态文字上移 22px，让 "Connected" 收在导航条（scene-y 228）之上。
    text_at(content, LV_SYMBOL_BLUETOOTH "  Passport Speaker", 18, 185,
            &lv_font_montserrat_14, t->text);
    text_at(content, "Connected", 18, 207,
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

    // hero 压到 84 高、orb 56：导航条常驻后底部只剩 228，dock 要坐在 168，
    // hero 必须在 160 前收住，否则与 dock 叠画。
    s_navigation.hero = solid_object(s_navigation.content, 14, 76, 184, 84,
                                     24, 0x3B93C5, LV_OPA_COVER);
    lv_obj_t *orb = solid_object(s_navigation.hero, 14, 14, 56, 56,
                                 LV_RADIUS_CIRCLE, t->text, 34);
    s_navigation.symbol = ui_glass_label(
        orb, "", &lv_font_montserrat_20, t->text);
    lv_obj_center(s_navigation.symbol);
    s_navigation.state_label = text_at(
        s_navigation.hero, "", 88, 22, &lv_font_montserrat_14, t->text);
    s_navigation.value_label = text_at(
        s_navigation.hero, "", 88, 46, &lv_font_montserrat_14, t->text_muted);

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
    // Dock 落在 168–224：hero 在 160 收住留 8px，footer 从 228 起留 4px。
    // 两块玻璃不再叠画也不再贴脸。选中块只在 x 轴动画，y 固定，改这里就够了。
    s_navigation.dock_shadow = solid_object(
        root, 16, 172, 208, 58, 30, 0x01070D, 44);
    s_navigation.dock = reference_glass_create(
        root, 18, 168, 204, 56, 28, 144, 1, t, NULL);
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

// ---- 实时数据页：Kaboo / Claude ----
//
// 这两页是实时数据页。与展示场景共用 plain_object/solid_object/text_at
// 与 theme()，但内容层用自己的 data_stage()：压暗层一路铺到屏幕底部并带向下
// 淡出的渐变，让导航条坐在同一片材质上而不是压在边缘（真机验证过的效果）。

static lv_obj_t *data_stage(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    // 96 与展示场景 Standard 模式一致；再高壁纸就成了一片死灰。
    solid_object(root, 0, 0, LIQUID_GLASS_COMPOSITOR_WIDTH,
                 SHOWCASE_SCENE_HEIGHT, 0, t->content_surface, 96);
    lv_obj_t *fade = solid_object(root, 0, 196, LIQUID_GLASS_COMPOSITOR_WIDTH,
                                  SHOWCASE_SCENE_HEIGHT - 196, 0,
                                  t->content_surface, LV_OPA_TRANSP);
    lv_obj_set_style_bg_grad_color(fade, lv_color_hex(t->content_surface), 0);
    lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(fade, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(fade, 96, 0);
    lv_obj_set_style_bg_opa(fade, LV_OPA_COVER, 0);
    return plain_object(root, 16, 4, 208, 204);
}

// token 计数压缩成 K/M/B，与 kaboo 自己的显示口径一致。
static void format_tokens(char *buf, size_t cap, uint64_t tokens)
{
    if (tokens >= 1000000000ull) {
        snprintf(buf, cap, "%llu.%lluB", tokens / 1000000000ull,
                 (tokens % 1000000000ull) / 100000000ull);
    } else if (tokens >= 1000000ull) {
        snprintf(buf, cap, "%llu.%lluM", tokens / 1000000ull,
                 (tokens % 1000000ull) / 100000ull);
    } else if (tokens >= 1000ull) {
        snprintf(buf, cap, "%llu.%lluK", tokens / 1000ull,
                 (tokens % 1000ull) / 100ull);
    } else {
        snprintf(buf, cap, "%llu", tokens);
    }
}

static void format_cost(char *buf, size_t cap, uint32_t cents)
{
    snprintf(buf, cap, "$%" PRIu32 ".%02" PRIu32, cents / 100u, cents % 100u);
}

// 距重置的剩余时间。窗口已翻篇时调用方不应走到这里。
static void format_remaining(char *buf, size_t cap, uint32_t seconds)
{
    if (seconds >= 3600u) {
        snprintf(buf, cap, "resets in %" PRIu32 "h %" PRIu32 "m",
                 seconds / 3600u, (seconds % 3600u) / 60u);
    } else {
        snprintf(buf, cap, "resets in %" PRIu32 "m", seconds / 60u);
    }
}

// 卡片下方的位置圆点：当前卡实心 accent，其余淡灰。
static void build_dots(lv_obj_t *parent, int y, lv_obj_t **dots, int count,
                       const ui_glass_theme_t *t)
{
    int span = count * 8 + (count - 1) * 14;
    int x = (208 - span) / 2;
    for (int i = 0; i < count; ++i) {
        dots[i] = solid_object(parent, x + i * 22, y, 8, 8, LV_RADIUS_CIRCLE,
                               t->text_muted, LV_OPA_40);
    }
}

static void dots_select(lv_obj_t **dots, int count, int active,
                        const ui_glass_theme_t *t)
{
    for (int i = 0; i < count; ++i) {
        if (!dots[i]) continue;
        bool on = (i == active);
        lv_obj_set_style_bg_color(dots[i],
                                  lv_color_hex(on ? t->accent : t->text_muted), 0);
        lv_obj_set_style_bg_opa(dots[i], on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

static void build_kaboo(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = data_stage(root);

    // 一张大玻璃卡承载当前窗口：窗口名 → 大字 token → 费用。
    lv_obj_t *card = ui_glass_platter_create(stage, 12, 28, 184, 118, t);
    s_kaboo_view.label = text_at(card, "", 0, 14, &lv_font_montserrat_14,
                                 t->text_muted);
    lv_obj_set_width(s_kaboo_view.label, 184);
    lv_obj_set_style_text_align(s_kaboo_view.label, LV_TEXT_ALIGN_CENTER, 0);

    s_kaboo_view.value = text_at(card, "--", 0, 36, &font_digits_44, t->text);
    lv_obj_set_width(s_kaboo_view.value, 184);
    lv_obj_set_style_text_align(s_kaboo_view.value, LV_TEXT_ALIGN_CENTER, 0);

    s_kaboo_view.cost = text_at(card, "", 0, 88, &lv_font_montserrat_14,
                                t->accent);
    lv_obj_set_width(s_kaboo_view.cost, 184);
    lv_obj_set_style_text_align(s_kaboo_view.cost, LV_TEXT_ALIGN_CENTER, 0);

    build_dots(stage, 156, s_kaboo_view.dots, KABOO_CARD_COUNT, t);

    // 模型名做成 accent 色胶囊，像 Activity 场景的状态 chip。
    lv_obj_t *chip = solid_object(stage, 12, 172, 184, 28, LV_RADIUS_CIRCLE,
                                  t->accent, LV_OPA_20);
    s_kaboo_view.model = text_at(chip, "--", 0, 6, &lv_font_montserrat_14,
                                 t->accent);
    lv_obj_set_width(s_kaboo_view.model, 184);
    lv_obj_set_style_text_align(s_kaboo_view.model, LV_TEXT_ALIGN_CENTER, 0);

    s_kaboo_view.note = text_at(stage, "", 12, 4, &lv_font_montserrat_14,
                                t->warning);
    lv_obj_set_width(s_kaboo_view.note, 184);
    lv_obj_set_style_text_align(s_kaboo_view.note, LV_TEXT_ALIGN_CENTER, 0);
}

// 一个配额窗口 = 一块玻璃卡片：标题 + 百分比 + 进度条 + 重置提示。
static void build_quota_card(lv_obj_t *stage, int y, const char *label,
                             lv_obj_t **out_value, lv_obj_t **out_bar,
                             lv_obj_t **out_reset)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *card = ui_glass_platter_create(stage, 12, y, 184, 82, t);

    text_at(card, label, 14, 10, &lv_font_montserrat_14, t->text);
    *out_value = text_at(card, "--", 100, 6, &lv_font_montserrat_20, t->text);
    lv_obj_set_width(*out_value, 70);
    lv_obj_set_style_text_align(*out_value, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *track = solid_object(card, 14, 40, 156, 8, LV_RADIUS_CIRCLE,
                                   t->text, LV_OPA_10);
    *out_bar = solid_object(track, 0, 0, 0, 8, LV_RADIUS_CIRCLE,
                            t->accent, LV_OPA_COVER);

    *out_reset = text_at(card, "", 14, 56, &lv_font_montserrat_14,
                         t->text_muted);
    lv_obj_set_width(*out_reset, 156);
}

// Claude 页两个窗口同屏各占一卡，不轮换 —— 配额只有两项，一眼看全更实用。
static void build_claude(lv_obj_t *root)
{
    const ui_glass_theme_t *t = theme();
    lv_obj_t *stage = data_stage(root);

    build_quota_card(stage, 27, "5 hour",
                     &s_claude_view.five_value, &s_claude_view.five_bar,
                     &s_claude_view.five_reset);
    build_quota_card(stage, 119, "7 day",
                     &s_claude_view.seven_value, &s_claude_view.seven_bar,
                     &s_claude_view.seven_reset);

    s_claude_view.note = text_at(stage, "", 12, 4, &lv_font_montserrat_14,
                                 t->warning);
    lv_obj_set_width(s_claude_view.note, 184);
    lv_obj_set_style_text_align(s_claude_view.note, LV_TEXT_ALIGN_CENTER, 0);
}

static void refresh_kaboo(const usage_snapshot_t *snap, bool have)
{
    static const char *const LABELS[KABOO_CARD_COUNT] = {
        "TODAY", "7 DAYS", "30 DAYS",
    };
    const ui_glass_theme_t *t = theme();
    if (!s_kaboo_view.value) return;

    lv_label_set_text(s_kaboo_view.label, LABELS[s_kaboo_card]);
    dots_select(s_kaboo_view.dots, KABOO_CARD_COUNT, s_kaboo_card, t);

    if (!have || !(snap->flags & USAGE_FLAG_KABOO_VALID)) {
        lv_label_set_text(s_kaboo_view.value, "--");
        lv_label_set_text(s_kaboo_view.cost, "");
        lv_label_set_text(s_kaboo_view.model, "--");
        lv_label_set_text(s_kaboo_view.note,
                          have ? "No kaboo data" : "Waiting for Mac");
        return;
    }

    uint64_t tokens = snap->today_tokens;
    uint32_t cents = snap->today_cost_cents;
    if (s_kaboo_card == 1) {
        tokens = snap->week_tokens;
        cents = snap->week_cost_cents;
    } else if (s_kaboo_card == 2) {
        tokens = snap->month_tokens;
        cents = snap->month_cost_cents;
    }

    char buf[32];
    format_tokens(buf, sizeof buf, tokens);
    lv_label_set_text(s_kaboo_view.value, buf);
    format_cost(buf, sizeof buf, cents);
    lv_label_set_text(s_kaboo_view.cost, buf);
    lv_label_set_text(s_kaboo_view.model,
                      snap->top_model[0] ? snap->top_model : "--");

    // 数据源过期时明确标注，而不是让旧数字冒充当前值。
    uint32_t now_unix = 0;
    if (usage_model_now_unix(snap, have, esp_timer_get_time(), &now_unix) &&
        !usage_model_source_fresh(snap->kaboo_sampled_unix, now_unix,
                                  SOURCE_TTL_SECONDS)) {
        lv_label_set_text(s_kaboo_view.note, "Data may be stale");
    } else {
        lv_label_set_text(s_kaboo_view.note, "");
    }
}

static void refresh_quota_row(lv_obj_t *value, lv_obj_t *bar, lv_obj_t *reset,
                              uint8_t pct, uint32_t resets_unix,
                              uint32_t now_unix, bool have_now)
{
    const ui_glass_theme_t *t = theme();

    lv_label_set_text_fmt(value, "%u%%", pct);
    lv_obj_set_width(bar, 156 * pct / 100);   // 轨道宽 156，与 build_quota_card 一致

    // 用量越高越接近告警色，让人一眼看出余量紧张。
    uint32_t color = t->accent;
    if (pct >= 90) color = t->danger;
    else if (pct >= 70) color = t->warning;
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);

    if (!have_now) {
        lv_label_set_text(reset, "");
        return;
    }
    if (usage_model_quota_expired(resets_unix, now_unix)) {
        lv_label_set_text(reset, "window elapsed");
        return;
    }
    char buf[40];
    format_remaining(buf, sizeof buf,
                     usage_model_seconds_until(resets_unix, now_unix));
    lv_label_set_text(reset, buf);
}

// 单个配额窗口无数据时的呈现：空轨道 + "--"，不是 0%。0% 是一个具体断言，
// 而"这个窗口当前没有数据"是另一回事。
static void blank_quota_row(lv_obj_t *value, lv_obj_t *bar, lv_obj_t *reset)
{
    lv_label_set_text(value, "--");
    lv_obj_set_width(bar, 0);
    lv_label_set_text(reset, "not active");
}

static void refresh_claude(const usage_snapshot_t *snap, bool have)
{
    if (!s_claude_view.five_value) return;

    if (!have || !(snap->flags & USAGE_FLAG_CLAUDE_VALID)) {
        blank_quota_row(s_claude_view.five_value, s_claude_view.five_bar,
                        s_claude_view.five_reset);
        blank_quota_row(s_claude_view.seven_value, s_claude_view.seven_bar,
                        s_claude_view.seven_reset);
        lv_label_set_text(s_claude_view.five_reset, "");
        lv_label_set_text(s_claude_view.seven_reset, "");
        lv_label_set_text(s_claude_view.note,
                          have ? "No quota data" : "Waiting for Mac");
        return;
    }

    uint32_t now_unix = 0;
    bool have_now = usage_model_now_unix(snap, have, esp_timer_get_time(),
                                         &now_unix);
    // 两个窗口各自判断：Claude Code 只在窗口活跃时才报它，缺失的那个显示
    // "not active" 而不是伪造 0%。
    if (snap->flags & USAGE_FLAG_FIVE_HOUR) {
        refresh_quota_row(s_claude_view.five_value, s_claude_view.five_bar,
                          s_claude_view.five_reset, snap->five_hour_pct,
                          snap->five_hour_resets_unix, now_unix, have_now);
    } else {
        blank_quota_row(s_claude_view.five_value, s_claude_view.five_bar,
                        s_claude_view.five_reset);
    }
    if (snap->flags & USAGE_FLAG_SEVEN_DAY) {
        refresh_quota_row(s_claude_view.seven_value, s_claude_view.seven_bar,
                          s_claude_view.seven_reset, snap->seven_day_pct,
                          snap->seven_day_resets_unix, now_unix, have_now);
    } else {
        blank_quota_row(s_claude_view.seven_value, s_claude_view.seven_bar,
                        s_claude_view.seven_reset);
    }

    if (have_now && !usage_model_source_fresh(snap->claude_sampled_unix,
                                              now_unix, SOURCE_TTL_SECONDS)) {
        lv_label_set_text(s_claude_view.note, "Data may be stale");
    } else {
        lv_label_set_text(s_claude_view.note, "");
    }
}

// 从 BLE 快照刷新当前数据页。展示场景不取快照——零 BLE 开销，且它们的
// view 指针在 stop_scene_activity 后为 NULL。
// 上一次真正重绘时的输入指纹。数据页每 200ms 被 tick 一次，但渲染出的文字
// 只在三种情况下变化：来了新 BLE 包、Kaboo 翻了卡、或者 Claude 倒计时跨过了
// 一分钟。其余 tick 全部跳过 —— 否则 21 个 label 每秒被重写 5 次，在没有
// PSRAM 的堆上是 100+ 次/秒的 malloc/free 空转。
static struct {
    uint32_t generation;
    uint8_t  card;
    uint32_t minute;
    bool     valid;
} s_data_drawn;

static void refresh_data_page(void)
{
    if (!is_data_page(s_page)) return;
    usage_snapshot_t snap;
    uint32_t generation = 0;
    bool have = usage_link_get(&snap, &generation);

    uint32_t now_unix = 0;
    uint32_t minute = 0;
    if (have && usage_model_now_unix(&snap, have, esp_timer_get_time(), &now_unix)) {
        minute = now_unix / 60u;
    }
    if (s_data_drawn.valid && s_data_drawn.generation == generation &&
        s_data_drawn.card == s_kaboo_card && s_data_drawn.minute == minute) {
        return;
    }
    s_data_drawn = (typeof(s_data_drawn)){
        .generation = generation, .card = s_kaboo_card,
        .minute = minute, .valid = true,
    };

    if (s_page == PAGE_KABOO) refresh_kaboo(&snap, have);
    else refresh_claude(&snap, have);
}

static void kaboo_card_advance(void)
{
    s_kaboo_card = (uint8_t)((s_kaboo_card + 1u) % KABOO_CARD_COUNT);
    s_card_elapsed_ms = 0;
    refresh_data_page();
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
    case PAGE_KABOO:             build_kaboo(content); break;
    case PAGE_CLAUDE:            build_claude(content); break;
    default:                     build_buttons(content); break;
    }
    // 数据页建好后立刻用最后一份快照填充，不等下一个 tick 留出空白帧。
    // 刚建的 label 是空的，必须作废上一次的指纹，否则同一份快照会被跳过。
    s_data_drawn.valid = false;
    refresh_data_page();
}

static void shell_refresh(void)
{
    const ui_glass_theme_t *t = theme();
    uint8_t position = page_position(s_page);
    // 标题只放页名。"N/10" 计数会把 "Appearance" 顶到 x=190，撞上右上角
    // 168 起的电量位；位置信息由导航条的邻居页名承担。
    lv_label_set_text(s_header_title, PAGE_TITLES[s_page]);
    set_label_color(s_header_title, t->text);
    // 导航条两侧都标出邻居页名：长按 OK / 双击 UP 去左边，UP 去右边。只写一侧会
    // 让人以为翻页是单向的。中间 3 个空格是余量：最宽的相邻对（Moments 页的
    // "Activity / Appearance"）在此格式下 187px，留 25px 给 212px 的平台。
    uint8_t left = (uint8_t)((position + SHOWCASE_PAGE_COUNT - 1u) %
                             SHOWCASE_PAGE_COUNT);
    uint8_t right = (uint8_t)((position + 1u) % SHOWCASE_PAGE_COUNT);
    lv_label_set_text_fmt(
        s_footer_label, LV_SYMBOL_LEFT "  %s   %s  " LV_SYMBOL_RIGHT,
        PAGE_TITLES[PAGE_ORDER[left]], PAGE_TITLES[PAGE_ORDER[right]]);
    set_label_color(s_footer_label, t->text_muted);
    ui_glass_surface_set_tint(s_footer, t->control_tint,
                              t->control_opacity);
    ui_glass_surface_set_material(s_footer, t->control_material);
    ui_glass_surface_set_edge_strength(s_footer, t->focus_edge_strength);
    // 导航条在每一页都可见：它现在承担导航信息，不再只是动作提示。
    lv_obj_remove_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
    link_dot_refresh();
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

// 步进表跑到尽头时调用；语义与原来删掉一次性 timer 相同。
static void scene_timer_finish(void)
{
    s_scene_done = true;
}

static void scene_showcase_step(void)
{
    s_scene_step++;
    switch (s_page) {
    case SHOWCASE_BUTTONS:
        if (s_scene_step == 1) focused_action();
        else if (s_scene_step == 2 || s_scene_step == 4) focus_move(1);
        else if (s_scene_step == 3 || s_scene_step == 5) focused_action();
        else scene_timer_finish();
        break;
    case SHOWCASE_SELECTION:
        if (s_scene_step == 1) focused_action();
        else if (s_scene_step == 2 || s_scene_step == 4) focus_move(1);
        else if (s_scene_step == 3) focused_action();
        else if (s_scene_step == 5) press_focused_component();
        else scene_timer_finish();
        break;
    case SHOWCASE_ADJUSTMENTS:
        if (s_scene_step == 1) focused_action();
        else if (s_scene_step == 2 || s_scene_step == 4) focus_move(1);
        else if (s_scene_step == 3 || s_scene_step == 5) focused_action();
        else scene_timer_finish();
        break;
    case SHOWCASE_LISTS:
        if (s_scene_step == 1 || s_scene_step == 2 || s_scene_step == 4) {
            focus_move(1);
        } else if (s_scene_step == 3 || s_scene_step == 5) {
            focused_action();
        } else {
            scene_timer_finish();
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
            scene_timer_finish();
        }
        break;
    case SHOWCASE_NAVIGATION:
        if (s_scene_step <= 3) navigation_select(1);
        else scene_timer_finish();
        break;
    case SHOWCASE_FEEDBACK:
        if (s_scene_step <= 3) focused_action();
        else scene_timer_finish();
        break;
    case SHOWCASE_STATES:
        // 无人巡航只演示焦点移动，不真的应用无障碍模式。模式存在共享
        // runtime 里，应用后会重新主题化全部十页；在无限循环的巡航里那意味着
        // Kaboo / Claude 每 70 秒换一次配色。用户按 OK 时才切换。
        if (s_scene_step <= 2) focus_move(1);
        else scene_timer_finish();
        break;
    default:
        scene_timer_finish();
        break;
    }
}

// 重置当前场景的步进演示。数据页没有步进表，直接标记完成。
static void start_scene_showcase(void)
{
    s_scene_step = 0;
    s_step_elapsed_ms = 0;
    s_scene_done = is_data_page(s_page);
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

// BLE 链路指示点。独立成函数是因为它必须每个 tick 都刷：只在翻页时刷的话，
// 用户停在一页上时 Mac 连上/断开都不会反映出来。
static void link_dot_refresh(void)
{
    if (!s_link_dot) return;
    const ui_glass_theme_t *t = theme();
    lv_obj_set_style_bg_color(
        s_link_dot,
        lv_color_hex(usage_link_connected() ? t->positive : t->text_muted), 0);
}

// 单一主定时器：巡航、场景步进、Kaboo 轮换、BLE 刷新全在这里按各自计数器推进。
static void master_tick(lv_timer_t *timer)
{
    (void)timer;
    // 链路点在转场期间也要刷，它不属于任何 scene。
    link_dot_refresh();
    if (s_transitioning) return;

    if (is_data_page(s_page)) {
        // 数据页：Kaboo 自动翻卡 + 从 BLE 快照刷新。手动翻卡后先暂停一会。
        if (s_page == PAGE_KABOO) {
            if (s_card_hold_ms > 0) {
                s_card_hold_ms = s_card_hold_ms > SHOWCASE_TICK_MS
                                     ? s_card_hold_ms - SHOWCASE_TICK_MS : 0;
            } else {
                s_card_elapsed_ms += SHOWCASE_TICK_MS;
                if (s_card_elapsed_ms >= CARD_ROTATE_MS) kaboo_card_advance();
            }
        }
        refresh_data_page();
        // 无人巡航只在展示场景之间走：自动翻走一个实时看板毫无意义。
        return;
    }

    // 展示场景：1 秒一步的页内演示，跑完即止。
    if (!s_scene_done) {
        s_step_elapsed_ms += SHOWCASE_TICK_MS;
        if (s_step_elapsed_ms >= SHOWCASE_SCENE_STEP_MS) {
            s_step_elapsed_ms = 0;
            scene_showcase_step();
        }
    }
    // 7 秒无人巡航，任何按键后永久停止。到 Appearance 后折返 Player，
    // 不进入数据页。
    if (!s_tour_killed) {
        s_tour_elapsed_ms += SHOWCASE_TICK_MS;
        if (s_tour_elapsed_ms >= SHOWCASE_TOUR_PERIOD_MS) {
            s_tour_elapsed_ms = 0;
            uint8_t next = (uint8_t)((page_position(s_page) + 1u) %
                                     SHOWCASE_PAGE_COUNT);
            if (is_data_page(PAGE_ORDER[next])) next = 0;
            show_page(PAGE_ORDER[next], 1, true);
        }
    }
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

void dashboard_set_battery(int soc)
{
    s_battery_soc = soc;
}

void dashboard_enter(void)
{
    s_page = SHOWCASE_OVERLAYS;
    s_transitioning = false;
    s_tour_killed = false;
    s_tour_elapsed_ms = 0;
    s_kaboo_card = 0;
    s_card_elapsed_ms = 0;
    s_card_hold_ms = 0;
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
    // 右上角：电量百分比 + BLE 链路指示点。固定几何，避免自动布局在页面
    // 切换瞬间因文字宽度变化而抖动。-1 时显示 "--%" 而不是画一个数字。
    s_battery_label = text_at(s_header_chrome, "--%", 168, 13,
                              &lv_font_montserrat_14, t->text);
    lv_obj_set_size(s_battery_label, 36, 20);
    lv_obj_set_style_text_align(s_battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    if (s_battery_soc >= 0) {
        lv_label_set_text_fmt(s_battery_label, "%d%%", s_battery_soc);
    }
    s_link_dot = solid_object(s_header_chrome, 210, 20, 10, 10,
                              LV_RADIUS_CIRCLE, t->text_muted, LV_OPA_COVER);
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
    // 主定时器最后创建：此时屏幕与 chrome 都已就位，第一个 tick 就能安全刷新。
    s_tick_timer = lv_timer_create(master_tick, SHOWCASE_TICK_MS, NULL);
}

void dashboard_exit(void)
{
    // 先停 timer 再删屏 —— 反过来会让回调访问已释放对象。
    // 这里不停 BLE：链路是应用级常驻服务，不属于任何单页。
    if (s_tick_timer) {
        lv_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }
    stop_tour();
    stop_scene_activity();
    lv_anim_delete(&s_page_motion, page_transition_set);
    s_transitioning = false;
    ui_glass_runtime_deinit(&s_runtime);
    s_scene = NULL;
    s_header_chrome = NULL;
    s_header_title = NULL;
    s_link_dot = NULL;
    s_battery_label = NULL;
    s_footer = NULL;
    s_footer_label = NULL;
    memset(&s_page_motion, 0, sizeof(s_page_motion));
    memset(&s_kaboo_view, 0, sizeof(s_kaboo_view));
    memset(&s_claude_view, 0, sizeof(s_claude_view));
}

void dashboard_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    // 任何按键都永久停止无人巡航，并让当前场景的自动演示让位给用户。
    stop_tour();
    stop_scene_timer();
    if (s_transitioning) return;

    if (event == BSP_BTN_DOUBLE) {
        if (button == BSP_BTN_UP) navigate_showcase_page(-1);
        return;
    }
    // 长按 OK = 上一页。导航条左侧标着邻居页名，得有一个单次动作能去那里；
    // 双击 UP 也行，但长按更容易被发现。原 demo 菜单的长按-返回已不存在。
    if (event == BSP_BTN_LONG) {
        if (button == BSP_BTN_OK) navigate_showcase_page(-1);
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

    // 数据页沿用同一约定：DOWN 切换页内状态（照 Activity 场景的先例），
    // OK 是焦点动作 —— Kaboo 只有一个可动的东西，就是当前卡片。
    if (s_page == PAGE_KABOO) {
        if (button == BSP_BTN_DOWN || button == BSP_BTN_OK) {
            kaboo_card_advance();
            s_card_hold_ms = CARD_MANUAL_HOLD_MS;
        }
        return;
    }
    if (s_page == PAGE_CLAUDE) {
        return;   // 两卡同屏无焦点，DOWN/OK 无操作
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
