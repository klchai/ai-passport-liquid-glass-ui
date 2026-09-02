#include "ui_glass_focus.h"
#include "ui_glass_motion.h"
#include "ui_glass_quality.h"
#include "ui_glass_theme.h"

#include <assert.h>
#include <stdio.h>

static void test_theme_fallback_and_modes(void)
{
    const ui_glass_theme_t *standard = ui_glass_theme_get(
        UI_GLASS_MODE_STANDARD);
    assert(ui_glass_theme_get((ui_glass_mode_t)99) == standard);
    assert(standard->control_material == UI_GLASS_MATERIAL_REGULAR);
    assert(ui_glass_theme_get(UI_GLASS_MODE_REDUCED_TRANSPARENCY)
               ->control_opacity > standard->control_opacity);
    assert(ui_glass_theme_get(UI_GLASS_MODE_HIGH_CONTRAST)
               ->focus_edge_strength > standard->focus_edge_strength);
}

static void test_motion_policy(void)
{
    assert(ui_glass_motion_duration(UI_GLASS_MODE_STANDARD,
                                    UI_GLASS_MOTION_MORPH) >= 500);
    assert(ui_glass_motion_duration(UI_GLASS_MODE_REDUCED_MOTION,
                                    UI_GLASS_MOTION_PAGE) == 0);
    assert(ui_glass_motion_duration(UI_GLASS_MODE_REDUCED_MOTION,
                                    UI_GLASS_MOTION_PRESS) > 0);
    assert(ui_glass_motion_duration(UI_GLASS_MODE_STANDARD,
                                    (ui_glass_motion_token_t)99) == 0);
}

static void test_focus_wrapping_and_clamping(void)
{
    ui_glass_focus_model_t focus;
    ui_glass_focus_init(&focus, 3, 0, true);
    assert(ui_glass_focus_move(&focus, -1));
    assert(focus.index == 2);
    assert(ui_glass_focus_move(&focus, 2));
    assert(focus.index == 1);

    ui_glass_focus_init(&focus, 3, 2, false);
    assert(!ui_glass_focus_move(&focus, 1));
    assert(focus.index == 2);
    assert(ui_glass_focus_move(&focus, -8));
    assert(focus.index == 0);
    ui_glass_focus_set_count(&focus, 0);
    assert(focus.index == 0 && focus.count == 0);
}

static void test_motion_curves_and_morph(void)
{
    assert(ui_glass_ease_out_cubic(0) == 0);
    assert(ui_glass_ease_out_cubic(UI_GLASS_MOTION_PROGRESS_MAX) ==
           UI_GLASS_MOTION_PROGRESS_MAX);
    assert(ui_glass_ease_out_cubic(512) > 512);
    assert(ui_glass_ease_in_out_cubic(512) == 512);
    assert(ui_glass_spring(768) > UI_GLASS_MOTION_PROGRESS_MAX);
    assert(ui_glass_spring(UI_GLASS_MOTION_PROGRESS_MAX) ==
           UI_GLASS_MOTION_PROGRESS_MAX);

    ui_glass_morph_frame_t start = {
        .x = 70, .y = 120, .width = 100, .height = 42,
        .radius = 21, .opacity = 120,
    };
    ui_glass_morph_frame_t end = {
        .x = 20, .y = 64, .width = 200, .height = 170,
        .radius = 18, .opacity = 220,
    };
    ui_glass_morph_frame_t result = ui_glass_morph_interpolate(
        start, end, UI_GLASS_MOTION_PROGRESS_MAX);
    assert(result.x == end.x && result.y == end.y);
    assert(result.width == end.width && result.height == end.height);
    assert(result.radius == end.radius && result.opacity == end.opacity);
}

static void test_adaptive_quality_hysteresis(void)
{
    ui_glass_quality_controller_t controller;
    ui_glass_quality_init(&controller, UI_GLASS_QUALITY_FULL);
    assert(ui_glass_quality_profile(controller.level)->glint_enabled);

    assert(!ui_glass_quality_observe(&controller, 500, 70000, 76800));
    assert(!ui_glass_quality_observe(&controller, 500, 70000, 76800));
    assert(ui_glass_quality_observe(&controller, 500, 70000, 76800));
    assert(controller.level == UI_GLASS_QUALITY_BALANCED);

    for (int i = 0; i < 5; ++i) {
        assert(!ui_glass_quality_observe(&controller, 120, 8000, 76800));
    }
    assert(ui_glass_quality_observe(&controller, 120, 8000, 76800));
    assert(controller.level == UI_GLASS_QUALITY_FULL);

    ui_glass_quality_set(&controller, UI_GLASS_QUALITY_ECONOMY, true);
    for (int i = 0; i < 8; ++i) {
        assert(!ui_glass_quality_observe(&controller, 80, 1000, 76800));
    }
    assert(controller.level == UI_GLASS_QUALITY_ECONOMY);
}

int main(void)
{
    test_theme_fallback_and_modes();
    test_motion_policy();
    test_focus_wrapping_and_clamping();
    test_motion_curves_and_morph();
    test_adaptive_quality_hysteresis();
    puts("ui_glass foundation tests: PASS");
    return 0;
}
