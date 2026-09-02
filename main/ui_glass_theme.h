#pragma once

#include "ui_glass_optics.h"

#include <stdbool.h>
#include <stdint.h>

// Design tokens are deliberately kept independent from LVGL so layout,
// accessibility, and motion policy can be covered by host tests.
#define UI_GLASS_SPACE_XS          4
#define UI_GLASS_SPACE_SM          8
#define UI_GLASS_SPACE_MD         12
#define UI_GLASS_SPACE_LG         16
#define UI_GLASS_RADIUS_CONTROL   14
#define UI_GLASS_RADIUS_PANEL     18
#define UI_GLASS_RADIUS_FLOATING  22
#define UI_GLASS_CONTROL_HEIGHT   42
#define UI_GLASS_CONTENT_LEFT     16
#define UI_GLASS_CONTENT_WIDTH   208

typedef enum {
    UI_GLASS_MODE_STANDARD = 0,
    UI_GLASS_MODE_HIGH_CONTRAST,
    UI_GLASS_MODE_REDUCED_TRANSPARENCY,
    UI_GLASS_MODE_REDUCED_MOTION,
    UI_GLASS_MODE_COUNT,
} ui_glass_mode_t;

typedef enum {
    UI_GLASS_MOTION_PRESS = 0,
    UI_GLASS_MOTION_FOCUS,
    UI_GLASS_MOTION_MORPH,
    UI_GLASS_MOTION_PAGE,
    UI_GLASS_MOTION_MATERIALIZE,
    UI_GLASS_MOTION_COUNT,
} ui_glass_motion_token_t;

typedef struct {
    const char *name;
    uint32_t text;
    uint32_t text_muted;
    uint32_t accent;
    uint32_t positive;
    uint32_t warning;
    uint32_t danger;
    uint32_t content_surface;
    uint32_t control_tint;
    uint8_t content_opacity;
    uint8_t control_opacity;
    uint8_t focus_edge_strength;
    ui_glass_material_t control_material;
    bool reduced_motion;
} ui_glass_theme_t;

// Invalid mode values resolve to the standard profile. Returned storage is
// immutable and remains valid for the lifetime of the firmware.
const ui_glass_theme_t *ui_glass_theme_get(ui_glass_mode_t mode);

// Returns the semantic duration for one interaction. Reduced Motion retains a
// short physical press response but removes spatial focus/page transitions.
uint16_t ui_glass_motion_duration(ui_glass_mode_t mode,
                                  ui_glass_motion_token_t token);
