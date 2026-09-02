#pragma once

#include "lvgl.h"
#include "ui_glass_theme.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_GLASS_COMPONENT_DEFAULT = 0,
    UI_GLASS_COMPONENT_FOCUSED,
    UI_GLASS_COMPONENT_PRESSED,
    UI_GLASS_COMPONENT_SELECTED,
    UI_GLASS_COMPONENT_DISABLED,
    UI_GLASS_COMPONENT_LOADING,
    UI_GLASS_COMPONENT_ERROR,
} ui_glass_component_state_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *label;
    lv_obj_t *value;
    lv_obj_t *indicator;
    lv_obj_t *auxiliary;
} ui_glass_component_t;

// Content panels are deliberately opaque. Glass is reserved for the control
// plane and transient presentation rather than used as a generic card style.
lv_obj_t *ui_glass_content_panel_create(lv_obj_t *parent,
                                        int x, int y, int width, int height,
                                        const ui_glass_theme_t *theme);

lv_obj_t *ui_glass_platter_create(lv_obj_t *parent,
                                  int x, int y, int width, int height,
                                  const ui_glass_theme_t *theme);

// One focus lens is moved among ordinary content rows. This keeps focus
// continuity visible without stacking a separate glass card behind every row.
lv_obj_t *ui_glass_focus_lens_create(lv_obj_t *parent,
                                     int x, int y, int width, int height,
                                     const ui_glass_theme_t *theme);
void ui_glass_focus_lens_apply_theme(lv_obj_t *lens,
                                     const ui_glass_theme_t *theme);

ui_glass_component_t ui_glass_row_create(lv_obj_t *parent,
                                         int x, int y, int width, int height,
                                         const char *label, const char *value,
                                         const ui_glass_theme_t *theme);
ui_glass_component_t ui_glass_toggle_create(
    lv_obj_t *parent, int x, int y, int width, int height,
    const char *label, bool enabled, const ui_glass_theme_t *theme);
ui_glass_component_t ui_glass_slider_create(
    lv_obj_t *parent, int x, int y, int width, int height,
    const char *label, uint8_t percent, const ui_glass_theme_t *theme);

void ui_glass_component_set_state(ui_glass_component_t *component,
                                  ui_glass_component_state_t state,
                                  const ui_glass_theme_t *theme);
void ui_glass_toggle_set(ui_glass_component_t *component, bool enabled,
                         const ui_glass_theme_t *theme);
void ui_glass_slider_set(ui_glass_component_t *component, uint8_t percent,
                         const ui_glass_theme_t *theme);

// Animated setters preserve the identity of the movable glass thumb. Calling
// either function again while motion is active continues from the thumb's
// current position, which keeps rapid physical-button input visually stable.
void ui_glass_toggle_set_animated(ui_glass_component_t *component,
                                  bool enabled,
                                  const ui_glass_theme_t *theme,
                                  uint16_t duration_ms);
void ui_glass_slider_set_animated(ui_glass_component_t *component,
                                  uint8_t percent,
                                  const ui_glass_theme_t *theme,
                                  uint16_t duration_ms);
