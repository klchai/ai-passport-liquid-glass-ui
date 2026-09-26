#include "ui_glass_optics.h"

// Average colors from a narrow center strip of the embedded wallpaper. The
// edge renderer interpolates this tiny LUT instead of retaining or reading a
// framebuffer, so a lens rim can still borrow plausible displaced color.
// tools/build_liquid_glass_wallpaper.py prints these rows when it rebuilds
// the wallpaper, and its --check-samples mode (run by validate.sh) fails if
// they no longer match the asset.
static const uint32_t WALLPAPER_CENTER_SAMPLES[] = {
    0x101113u, 0x131416u, 0x151719u, 0x181A1Du, 0x1C1E21u,
    0x202124u, 0x222327u, 0x202125u, 0x2B2D31u, 0x292A2Eu,
    0x27292Du, 0x27282Cu, 0x242529u, 0x575A5Fu, 0x404247u,
    0x37393Eu, 0x313337u, 0x73757Au, 0x4D5055u, 0x44464Au,
};

ui_glass_optics_t ui_glass_optics_for_material(ui_glass_material_t material)
{
    static const ui_glass_optics_t profiles[] = {
        [UI_GLASS_MATERIAL_REGULAR] = {
            .fill_opacity = 158,
            .stack_fill_opacity = 67,
            .edge_strength = 86,
            .ring_opacity = { 72, 26, 7 },
            .top_specular_opacity = 76,
            .bottom_refraction_opacity = 34,
            .glint_opacity = 218,
            .glint_width_percent = 28,
        },
        [UI_GLASS_MATERIAL_CLEAR] = {
            .fill_opacity = 92,
            .stack_fill_opacity = 52,
            .edge_strength = 102,
            .ring_opacity = { 92, 34, 9 },
            .top_specular_opacity = 88,
            .bottom_refraction_opacity = 42,
            .glint_opacity = 236,
            .glint_width_percent = 34,
        },
        [UI_GLASS_MATERIAL_CONTRAST] = {
            .fill_opacity = 210,
            .stack_fill_opacity = 84,
            .edge_strength = 76,
            .ring_opacity = { 60, 20, 6 },
            .top_specular_opacity = 64,
            .bottom_refraction_opacity = 30,
            .glint_opacity = 184,
            .glint_width_percent = 24,
        },
        [UI_GLASS_MATERIAL_CHROME] = {
            .fill_opacity = 30,
            .stack_fill_opacity = 30,
            .edge_strength = 30,
            .ring_opacity = { 46, 16, 4 },
            .top_specular_opacity = 48,
            .bottom_refraction_opacity = 22,
            .glint_opacity = 0,
            .glint_width_percent = 0,
        },
    };

    if (material < UI_GLASS_MATERIAL_REGULAR ||
        material > UI_GLASS_MATERIAL_CHROME) {
        material = UI_GLASS_MATERIAL_REGULAR;
    }
    return profiles[material];
}

uint8_t ui_glass_scale_opacity(uint8_t opacity, uint8_t edge_strength)
{
    uint16_t scaled = ((uint16_t)opacity * edge_strength + 43u) / 86u;
    return (uint8_t)(scaled > 255u ? 255u : scaled);
}

uint32_t ui_glass_mix_rgb(uint32_t first, uint32_t second, uint8_t mix)
{
    uint16_t inverse = (uint16_t)(255u - mix);
    uint8_t red = (uint8_t)((((first >> 16) & 0xFFu) * inverse +
                             ((second >> 16) & 0xFFu) * mix + 127u) / 255u);
    uint8_t green = (uint8_t)((((first >> 8) & 0xFFu) * inverse +
                               ((second >> 8) & 0xFFu) * mix + 127u) / 255u);
    uint8_t blue = (uint8_t)(((first & 0xFFu) * inverse +
                              (second & 0xFFu) * mix + 127u) / 255u);
    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

uint32_t ui_glass_background_at_y(int16_t y)
{
    if (y < 0) y = 0;
    if (y > 319) y = 319;

    const uint32_t last =
        (sizeof(WALLPAPER_CENTER_SAMPLES) /
         sizeof(WALLPAPER_CENTER_SAMPLES[0])) - 1u;
    uint32_t position = ((uint32_t)y * last * 256u) / 319u;
    uint32_t index = position >> 8;
    if (index >= last) return WALLPAPER_CENTER_SAMPLES[last];

    return ui_glass_mix_rgb(WALLPAPER_CENTER_SAMPLES[index],
                            WALLPAPER_CENTER_SAMPLES[index + 1u],
                            (uint8_t)(position & 0xFFu));
}

int16_t ui_glass_glint_center(int32_t progress, int16_t left, int16_t right)
{
    int32_t width = (int32_t)right - left;
    return (int16_t)(left + (width * progress) / UI_GLASS_ANIM_PROGRESS_MAX);
}

int16_t ui_glass_effective_radius(int16_t radius, int16_t width,
                                  int16_t height)
{
    // Same rule as LVGL's `short_side >> 1` clamp. The radius is only compared,
    // never added to or doubled, so LV_RADIUS_CIRCLE cannot overflow int16_t.
    int16_t short_side = width < height ? width : height;
    if (short_side <= 0 || radius <= 0) return 0;
    int16_t limit = (int16_t)(short_side / 2);
    return radius < limit ? radius : limit;
}
