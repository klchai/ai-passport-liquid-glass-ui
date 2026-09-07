#pragma once

#include "lvgl.h"
#include "ui_glass_optics.h"
#include <stdint.h>

#define UI_GLASS_BG_TOP       0x071018
#define UI_GLASS_BG_BOTTOM    0x151D27
#define UI_GLASS_TEXT         0xF4F7FA
#define UI_GLASS_TEXT_MUTED   0x99A8B6
#define UI_GLASS_ACCENT       0x8DDCFF
#define UI_GLASS_GLINT_HIDDEN (-2048)

// Creates the root canvas. The scanline compositor owns the native RGB565
// wallpaper; a compact sampled-color LUT lets the edge renderer approximate
// displaced background colors without retaining a framebuffer.
lv_obj_t *ui_glass_screen_create(void);

// Creates a lightweight glass surface. The center is a flat neutral tint; only
// a three-pixel edge profile and local specular segments receive custom draws.
lv_obj_t *ui_glass_surface_create(lv_obj_t *parent, int x, int y, int w, int h,
                                  int radius, uint32_t tint, lv_opa_t opacity,
                                  ui_glass_material_t material);

// Changes the optical profile without recreating the surface. Fill opacity is
// owned by the caller so deck geometry and material transmittance stay separate.
void ui_glass_surface_set_material(lv_obj_t *surface,
                                   ui_glass_material_t material);

// Updates both the ordinary fill style and the tint used by the optical edge
// renderer. This is required when accessibility modes retheme a persistent
// platter without recreating the whole screen.
void ui_glass_surface_set_tint(lv_obj_t *surface, uint32_t tint,
                               lv_opa_t opacity);

// Edge strength is renderer state rather than an LVGL style: changing it only
// invalidates the three-pixel optical rim instead of repainting the card fill.
void ui_glass_surface_set_edge_strength(lv_obj_t *surface, uint8_t strength);
uint8_t ui_glass_surface_get_edge_strength(lv_obj_t *surface);

// Delegates the static three-ring rim and local highlights to the fused
// background compositor. The surface keeps only its moving glint draw.
void ui_glass_surface_set_fused_edge(lv_obj_t *surface, bool fused);

// Suppresses the multi-ring optical border and specular highlights during fast
// transitions to preserve high frame rate without CPU saturation.
void ui_glass_set_optics_suppressed(bool suppressed);
bool ui_glass_is_optics_suppressed(void);

// Updates the one-shot specular sweep. Progress may extend beyond 0..1024 so
// the highlight enters and leaves outside the visible edge. Only the old/new
// two-pixel highlight footprint is invalidated.
void ui_glass_surface_set_glint(lv_obj_t *surface, int32_t progress);

// Creates unboxed text with the shared typography and foreground treatment.
lv_obj_t *ui_glass_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color);
