#include "ui_glass_quality.h"

static const ui_glass_quality_profile_t PROFILES[UI_GLASS_QUALITY_COUNT] = {
    [UI_GLASS_QUALITY_FULL] = {
        .name = "Full",
        .refresh_period_ms = 10,
        .animated_glass_limit = 6,
        .edge_strength_percent = 100,
        .glint_enabled = true,
    },
    [UI_GLASS_QUALITY_BALANCED] = {
        .name = "Balanced",
        .refresh_period_ms = 16,
        .animated_glass_limit = 3,
        .edge_strength_percent = 86,
        .glint_enabled = true,
    },
    [UI_GLASS_QUALITY_ECONOMY] = {
        .name = "Economy",
        .refresh_period_ms = 33,
        .animated_glass_limit = 1,
        .edge_strength_percent = 70,
        .glint_enabled = false,
    },
};

const ui_glass_quality_profile_t *ui_glass_quality_profile(
    ui_glass_quality_t quality)
{
    if (quality < UI_GLASS_QUALITY_FULL ||
        quality >= UI_GLASS_QUALITY_COUNT) {
        quality = UI_GLASS_QUALITY_BALANCED;
    }
    return &PROFILES[quality];
}

void ui_glass_quality_init(ui_glass_quality_controller_t *controller,
                           ui_glass_quality_t initial)
{
    if (!controller) return;
    if (initial < UI_GLASS_QUALITY_FULL ||
        initial >= UI_GLASS_QUALITY_COUNT) {
        initial = UI_GLASS_QUALITY_BALANCED;
    }
    controller->level = initial;
    controller->slow_samples = 0;
    controller->fast_samples = 0;
    controller->locked = false;
}

void ui_glass_quality_set(ui_glass_quality_controller_t *controller,
                          ui_glass_quality_t quality,
                          bool locked)
{
    if (!controller) return;
    if (quality < UI_GLASS_QUALITY_FULL ||
        quality >= UI_GLASS_QUALITY_COUNT) {
        quality = UI_GLASS_QUALITY_BALANCED;
    }
    controller->level = quality;
    controller->slow_samples = 0;
    controller->fast_samples = 0;
    controller->locked = locked;
}

bool ui_glass_quality_observe(ui_glass_quality_controller_t *controller,
                              uint16_t submit_ms_x10,
                              uint32_t pixels_per_update,
                              uint32_t screen_pixels)
{
    if (!controller || controller->locked || screen_pixels == 0) return false;

    uint32_t coverage_percent = pixels_per_update * 100u / screen_pixels;
    uint16_t slow_limit = controller->level == UI_GLASS_QUALITY_FULL
                              ? 300u : 450u;
    uint16_t fast_limit = controller->level == UI_GLASS_QUALITY_ECONOMY
                              ? 300u : 220u;
    bool slow = submit_ms_x10 > slow_limit || coverage_percent > 72u;
    bool fast = submit_ms_x10 < fast_limit && coverage_percent < 42u;

    controller->slow_samples = slow ? controller->slow_samples + 1u : 0u;
    controller->fast_samples = fast ? controller->fast_samples + 1u : 0u;

    if (controller->slow_samples >= 3u &&
        controller->level < UI_GLASS_QUALITY_ECONOMY) {
        controller->level++;
        controller->slow_samples = 0;
        controller->fast_samples = 0;
        return true;
    }
    if (controller->fast_samples >= 6u &&
        controller->level > UI_GLASS_QUALITY_FULL) {
        controller->level--;
        controller->slow_samples = 0;
        controller->fast_samples = 0;
        return true;
    }
    return false;
}
