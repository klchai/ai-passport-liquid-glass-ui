#include <assert.h>
#include "ui_glass_optics.h"

int main(void)
{
    ui_glass_optics_t regular =
        ui_glass_optics_for_material(UI_GLASS_MATERIAL_REGULAR);
    ui_glass_optics_t clear =
        ui_glass_optics_for_material(UI_GLASS_MATERIAL_CLEAR);
    ui_glass_optics_t contrast =
        ui_glass_optics_for_material(UI_GLASS_MATERIAL_CONTRAST);

    assert(clear.fill_opacity < regular.fill_opacity);
    assert(contrast.fill_opacity > regular.fill_opacity);
    assert(clear.stack_fill_opacity < regular.stack_fill_opacity);
    assert(contrast.stack_fill_opacity > regular.stack_fill_opacity);
    assert(clear.edge_strength > regular.edge_strength);
    assert(regular.fill_opacity < 192);
    assert(clear.fill_opacity < 128);

    for (int material = UI_GLASS_MATERIAL_REGULAR;
         material <= UI_GLASS_MATERIAL_CHROME; ++material) {
        ui_glass_optics_t optics =
            ui_glass_optics_for_material((ui_glass_material_t)material);
        assert(optics.ring_opacity[0] > optics.ring_opacity[1]);
        assert(optics.ring_opacity[1] > optics.ring_opacity[2]);
        assert(optics.top_specular_opacity >
               optics.bottom_refraction_opacity);
        assert(optics.stack_fill_opacity <= optics.fill_opacity);
    }

    // Equal cards use equal per-layer alpha. Three stacked layers remain
    // readable and preserve the intended clear < regular < contrast ordering.
    uint8_t stack_alpha[3] = {
        clear.stack_fill_opacity,
        regular.stack_fill_opacity,
        contrast.stack_fill_opacity,
    };
    uint16_t combined[3];
    for (int material = 0; material < 3; ++material) {
        uint32_t transmission = 255 - stack_alpha[material];
        transmission = (transmission * transmission + 127) / 255;
        transmission = (transmission * (255 - stack_alpha[material]) + 127) /
                       255;
        combined[material] = (uint16_t)(255 - transmission);
        assert(combined[material] >= 120 && combined[material] <= 185);
    }
    assert(combined[0] < combined[1] && combined[1] < combined[2]);

    assert(ui_glass_scale_opacity(86, 86) == 86);
    assert(ui_glass_scale_opacity(255, 255) == 255);
    assert(ui_glass_scale_opacity(100, 43) == 50);

    assert(ui_glass_mix_rgb(0x000000, 0xFFFFFF, 0) == 0x000000);
    assert(ui_glass_mix_rgb(0x000000, 0xFFFFFF, 255) == 0xFFFFFF);
    assert(ui_glass_background_at_y(-100) == ui_glass_background_at_y(0));
    assert(ui_glass_background_at_y(500) == ui_glass_background_at_y(319));
    assert(ui_glass_background_at_y(0) != ui_glass_background_at_y(160));

    assert(ui_glass_glint_center(0, 10, 210) == 10);
    assert(ui_glass_glint_center(UI_GLASS_ANIM_PROGRESS_MAX, 10, 210) == 210);
    assert(ui_glass_glint_center(-256, 10, 210) < 10);
    assert(ui_glass_glint_center(1280, 10, 210) > 210);
    return 0;
}
