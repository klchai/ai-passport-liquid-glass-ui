#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_GLASS_QUALITY_FULL = 0,
    UI_GLASS_QUALITY_BALANCED,
    UI_GLASS_QUALITY_ECONOMY,
    UI_GLASS_QUALITY_COUNT,
} ui_glass_quality_t;

typedef struct {
    const char *name;
    uint16_t refresh_period_ms;
    uint8_t animated_glass_limit;
    uint8_t edge_strength_percent;
    bool glint_enabled;
} ui_glass_quality_profile_t;

typedef struct {
    ui_glass_quality_t level;
    uint8_t slow_samples;
    uint8_t fast_samples;
    bool locked;
} ui_glass_quality_controller_t;

const ui_glass_quality_profile_t *ui_glass_quality_profile(
    ui_glass_quality_t quality);
void ui_glass_quality_init(ui_glass_quality_controller_t *controller,
                           ui_glass_quality_t initial);
void ui_glass_quality_set(ui_glass_quality_controller_t *controller,
                          ui_glass_quality_t quality,
                          bool locked);

// Observes a one-second display sample. The hysteresis intentionally reacts
// slowly so transient full-screen transitions do not make quality oscillate.
// Returns true only when the quality level changes.
bool ui_glass_quality_observe(ui_glass_quality_controller_t *controller,
                              uint16_t submit_ms_x10,
                              uint32_t pixels_per_update,
                              uint32_t screen_pixels);
