#include "ui_glass_theme.h"

static const ui_glass_theme_t THEMES[UI_GLASS_MODE_COUNT] = {
    [UI_GLASS_MODE_STANDARD] = {
        .name = "Standard",
        .text = 0xF5F8FB,
        .text_muted = 0xAABAC8,
        .accent = 0x9FE4FF,
        .positive = 0x83E7B8,
        .warning = 0xFFD37A,
        .danger = 0xFF949A,
        .content_surface = 0x071421,
        .control_tint = 0x526B7D,
        .content_opacity = 252,
        .control_opacity = 154,
        .focus_edge_strength = 126,
        .control_material = UI_GLASS_MATERIAL_REGULAR,
        .reduced_motion = false,
    },
    [UI_GLASS_MODE_HIGH_CONTRAST] = {
        .name = "High contrast",
        .text = 0xFFFFFF,
        .text_muted = 0xD6E0E8,
        .accent = 0xBDEFFF,
        .positive = 0xA6F3CF,
        .warning = 0xFFE19B,
        .danger = 0xFFB4B8,
        .content_surface = 0x050C13,
        .control_tint = 0x263E50,
        .content_opacity = 255,
        .control_opacity = 218,
        .focus_edge_strength = 148,
        .control_material = UI_GLASS_MATERIAL_CONTRAST,
        .reduced_motion = false,
    },
    [UI_GLASS_MODE_REDUCED_TRANSPARENCY] = {
        .name = "Less transparency",
        .text = 0xF8FAFC,
        .text_muted = 0xC5D0D9,
        .accent = 0xA9E8FF,
        .positive = 0x91EAC1,
        .warning = 0xFFDA8A,
        .danger = 0xFFA3A8,
        .content_surface = 0x0A1722,
        .control_tint = 0x1D3444,
        .content_opacity = 255,
        .control_opacity = 236,
        .focus_edge_strength = 136,
        .control_material = UI_GLASS_MATERIAL_CONTRAST,
        .reduced_motion = false,
    },
    [UI_GLASS_MODE_REDUCED_MOTION] = {
        .name = "Reduced motion",
        .text = 0xF5F8FB,
        .text_muted = 0xAABAC8,
        .accent = 0x9FE4FF,
        .positive = 0x83E7B8,
        .warning = 0xFFD37A,
        .danger = 0xFF949A,
        .content_surface = 0x071421,
        .control_tint = 0x526B7D,
        .content_opacity = 252,
        .control_opacity = 154,
        .focus_edge_strength = 126,
        .control_material = UI_GLASS_MATERIAL_REGULAR,
        .reduced_motion = true,
    },
};

const ui_glass_theme_t *ui_glass_theme_get(ui_glass_mode_t mode)
{
    if (mode < UI_GLASS_MODE_STANDARD || mode >= UI_GLASS_MODE_COUNT) {
        mode = UI_GLASS_MODE_STANDARD;
    }
    return &THEMES[mode];
}

uint16_t ui_glass_motion_duration(ui_glass_mode_t mode,
                                  ui_glass_motion_token_t token)
{
    static const uint16_t durations[UI_GLASS_MOTION_COUNT] = {
        [UI_GLASS_MOTION_PRESS] = 120,
        [UI_GLASS_MOTION_FOCUS] = 320,
        [UI_GLASS_MOTION_MORPH] = 560,
        [UI_GLASS_MOTION_PAGE] = 400,
        [UI_GLASS_MOTION_MATERIALIZE] = 520,
    };
    if (token < UI_GLASS_MOTION_PRESS || token >= UI_GLASS_MOTION_COUNT) {
        return 0;
    }
    const ui_glass_theme_t *theme = ui_glass_theme_get(mode);
    if (theme->reduced_motion) {
        return token == UI_GLASS_MOTION_PRESS ? 80 : 0;
    }
    return durations[token];
}
