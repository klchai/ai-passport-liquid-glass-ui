#pragma once

#include "lvgl.h"
#include "ui_glass_compositor.h"
#include "ui_glass_quality.h"
#include "ui_glass_theme.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    lv_obj_t *screen;
    ui_glass_compositor_t *backdrop;
    lv_timer_t *refresh_timer;
    ui_glass_mode_t mode;
    ui_glass_quality_controller_t quality;
} ui_glass_runtime_t;

// Creates a screen, native RGB565 wallpaper compositor, and bounded refresh
// policy. Call only while the LVGL lock is held.
bool ui_glass_runtime_init(ui_glass_runtime_t *runtime,
                           ui_glass_mode_t mode,
                           ui_glass_quality_t quality);

// Restores the repository-wide LVGL refresh period and deletes the owned
// screen. The page must stop its own timers and animations first.
void ui_glass_runtime_deinit(ui_glass_runtime_t *runtime);

const ui_glass_theme_t *ui_glass_runtime_theme(
    const ui_glass_runtime_t *runtime);
void ui_glass_runtime_set_mode(ui_glass_runtime_t *runtime,
                               ui_glass_mode_t mode);
void ui_glass_runtime_set_quality(ui_glass_runtime_t *runtime,
                                  ui_glass_quality_t quality,
                                  bool locked);

// Feeds display telemetry into the adaptive quality controller and applies a
// new refresh period if the hysteresis changes level.
bool ui_glass_runtime_observe(ui_glass_runtime_t *runtime,
                              uint16_t submit_ms_x10,
                              uint32_t pixels_per_update);
