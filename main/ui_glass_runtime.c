#include "ui_glass_runtime.h"

#include "ui_glass.h"

#include <string.h>

static void apply_quality(ui_glass_runtime_t *runtime)
{
    if (!runtime || !runtime->refresh_timer) return;
    const ui_glass_quality_profile_t *profile =
        ui_glass_quality_profile(runtime->quality.level);
    lv_timer_set_period(runtime->refresh_timer, profile->refresh_period_ms);
}

bool ui_glass_runtime_init(ui_glass_runtime_t *runtime,
                           ui_glass_mode_t mode,
                           ui_glass_quality_t quality)
{
    if (!runtime) return false;
    memset(runtime, 0, sizeof(*runtime));
    runtime->mode = mode;
    ui_glass_quality_init(&runtime->quality, quality);
    runtime->screen = ui_glass_screen_create();
    if (!runtime->screen) return false;

    const ui_glass_theme_t *theme = ui_glass_theme_get(mode);
    runtime->backdrop = ui_glass_compositor_create(
        runtime->screen, theme->control_tint, theme->control_material);
    if (!runtime->backdrop) {
        lv_obj_delete(runtime->screen);
        memset(runtime, 0, sizeof(*runtime));
        return false;
    }

    runtime->refresh_timer = lv_display_get_refr_timer(
        lv_obj_get_display(runtime->screen));
    apply_quality(runtime);
    return true;
}

void ui_glass_runtime_deinit(ui_glass_runtime_t *runtime)
{
    if (!runtime) return;
    if (runtime->refresh_timer) {
        lv_timer_set_period(runtime->refresh_timer, LV_DEF_REFR_PERIOD);
    }
    if (runtime->screen) lv_obj_delete(runtime->screen);
    memset(runtime, 0, sizeof(*runtime));
}

const ui_glass_theme_t *ui_glass_runtime_theme(
    const ui_glass_runtime_t *runtime)
{
    return ui_glass_theme_get(runtime ? runtime->mode :
                              UI_GLASS_MODE_STANDARD);
}

void ui_glass_runtime_set_mode(ui_glass_runtime_t *runtime,
                               ui_glass_mode_t mode)
{
    if (!runtime) return;
    runtime->mode = mode;
}

void ui_glass_runtime_set_quality(ui_glass_runtime_t *runtime,
                                  ui_glass_quality_t quality,
                                  bool locked)
{
    if (!runtime) return;
    ui_glass_quality_set(&runtime->quality, quality, locked);
    apply_quality(runtime);
}

bool ui_glass_runtime_observe(ui_glass_runtime_t *runtime,
                              uint16_t submit_ms_x10,
                              uint32_t pixels_per_update)
{
    if (!runtime) return false;
    bool changed = ui_glass_quality_observe(
        &runtime->quality, submit_ms_x10, pixels_per_update,
        LIQUID_GLASS_COMPOSITOR_WIDTH * LIQUID_GLASS_COMPOSITOR_HEIGHT);
    if (changed) apply_quality(runtime);
    return changed;
}
