#include "ui_glass_widgets.h"

#include "ui_glass.h"

#include <string.h>

static lv_obj_t *plain_object(lv_obj_t *parent, int x, int y,
                              int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    return object;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = ui_glass_label(parent, text, font, color);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    return label;
}

static void position_x_set(void *object, int32_t x)
{
    if (object) lv_obj_set_x((lv_obj_t *)object, x);
}

static void glint_set(void *object, int32_t progress)
{
    if (object) ui_glass_surface_set_glint((lv_obj_t *)object, progress);
}

static void animate_thumb(lv_obj_t *thumb, int32_t target_x,
                          uint16_t duration_ms)
{
    if (!thumb) return;
    lv_anim_delete(thumb, position_x_set);
    lv_anim_delete(thumb, glint_set);

    if (duration_ms == 0 || lv_obj_get_x(thumb) == target_x) {
        lv_obj_set_x(thumb, target_x);
        ui_glass_surface_set_glint(thumb, UI_GLASS_GLINT_HIDDEN);
        return;
    }

    lv_anim_t motion;
    lv_anim_init(&motion);
    lv_anim_set_var(&motion, thumb);
    lv_anim_set_exec_cb(&motion, position_x_set);
    lv_anim_set_values(&motion, lv_obj_get_x(thumb), target_x);
    lv_anim_set_duration(&motion, duration_ms);
    lv_anim_set_path_cb(&motion, lv_anim_path_ease_out);
    lv_anim_start(&motion);

    lv_anim_t glint;
    lv_anim_init(&glint);
    lv_anim_set_var(&glint, thumb);
    lv_anim_set_exec_cb(&glint, glint_set);
    lv_anim_set_values(&glint, -256, 1280);
    lv_anim_set_duration(&glint, duration_ms);
    lv_anim_set_path_cb(&glint, lv_anim_path_ease_in_out);
    lv_anim_start(&glint);
}

lv_obj_t *ui_glass_content_panel_create(lv_obj_t *parent,
                                        int x, int y, int width, int height,
                                        const ui_glass_theme_t *theme)
{
    if (!theme) theme = ui_glass_theme_get(UI_GLASS_MODE_STANDARD);
    lv_obj_t *panel = plain_object(parent, x, y, width, height);
    lv_obj_set_style_radius(panel, UI_GLASS_RADIUS_PANEL, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(theme->content_surface), 0);
    lv_obj_set_style_bg_opa(panel, theme->content_opacity, 0);
    return panel;
}

lv_obj_t *ui_glass_platter_create(lv_obj_t *parent,
                                  int x, int y, int width, int height,
                                  const ui_glass_theme_t *theme)
{
    if (!theme) theme = ui_glass_theme_get(UI_GLASS_MODE_STANDARD);
    lv_obj_t *platter = ui_glass_surface_create(
        parent, x, y, width, height, UI_GLASS_RADIUS_FLOATING,
        theme->control_tint, theme->control_opacity,
        theme->control_material);
    ui_glass_surface_set_edge_strength(platter, theme->focus_edge_strength);
    return platter;
}

lv_obj_t *ui_glass_focus_lens_create(lv_obj_t *parent,
                                     int x, int y, int width, int height,
                                     const ui_glass_theme_t *theme)
{
    if (!theme) theme = ui_glass_theme_get(UI_GLASS_MODE_STANDARD);
    lv_obj_t *lens = ui_glass_surface_create(
        parent, x, y, width, height, UI_GLASS_RADIUS_CONTROL,
        theme->accent, 38, UI_GLASS_MATERIAL_CLEAR);
    ui_glass_focus_lens_apply_theme(lens, theme);
    return lens;
}

void ui_glass_focus_lens_apply_theme(lv_obj_t *lens,
                                     const ui_glass_theme_t *theme)
{
    if (!lens || !theme) return;
    ui_glass_surface_set_tint(
        lens, theme->accent,
        theme->control_material == UI_GLASS_MATERIAL_CONTRAST ? 68 : 38);
    ui_glass_surface_set_material(lens, theme->control_material);
    ui_glass_surface_set_edge_strength(lens, theme->focus_edge_strength);
}

ui_glass_component_t ui_glass_row_create(lv_obj_t *parent,
                                         int x, int y, int width, int height,
                                         const char *label, const char *value,
                                         const ui_glass_theme_t *theme)
{
    if (!theme) theme = ui_glass_theme_get(UI_GLASS_MODE_STANDARD);
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, x, y, width, height);
    component.label = label_create(component.root, label,
                                   &lv_font_montserrat_14, theme->text);
    int value_width = value ? (strlen(value) <= 2 ? 24 : 60) : 0;
    lv_obj_set_width(component.label,
                     width - UI_GLASS_SPACE_MD * 2 -
                     (value ? value_width + 8 : 0));
    lv_label_set_long_mode(component.label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(component.label, lv_font_montserrat_14.line_height);
    lv_obj_align(component.label, LV_ALIGN_LEFT_MID,
                 UI_GLASS_SPACE_MD, -1);
    if (value) {
        component.value = label_create(component.root, value,
                                       &lv_font_montserrat_14,
                                       theme->text_muted);
        lv_obj_set_width(component.value, value_width);
        lv_label_set_long_mode(component.value, LV_LABEL_LONG_DOT);
        lv_obj_set_height(component.value, lv_font_montserrat_14.line_height);
        lv_obj_set_style_text_align(component.value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(component.value, LV_ALIGN_RIGHT_MID,
                     -UI_GLASS_SPACE_MD, -1);
    }
    return component;
}

ui_glass_component_t ui_glass_toggle_create(
    lv_obj_t *parent, int x, int y, int width, int height,
    const char *label, bool enabled, const ui_glass_theme_t *theme)
{
    if (!theme) theme = ui_glass_theme_get(UI_GLASS_MODE_STANDARD);
    ui_glass_component_t component = ui_glass_row_create(
        parent, x, y, width, height, label, NULL, theme);
    lv_obj_set_width(component.label, width - UI_GLASS_SPACE_MD - 62);
    component.indicator = plain_object(component.root, width - 54, 9, 42, 24);
    lv_obj_set_style_radius(component.indicator, LV_RADIUS_CIRCLE, 0);
    component.auxiliary = ui_glass_surface_create(
        component.indicator, 2, 2, 20, 20, LV_RADIUS_CIRCLE,
        theme->text, LV_OPA_90, UI_GLASS_MATERIAL_CLEAR);
    ui_glass_surface_set_edge_strength(component.auxiliary, 112);
    ui_glass_toggle_set(&component, enabled, theme);
    return component;
}

ui_glass_component_t ui_glass_slider_create(
    lv_obj_t *parent, int x, int y, int width, int height,
    const char *label, uint8_t percent, const ui_glass_theme_t *theme)
{
    if (!theme) theme = ui_glass_theme_get(UI_GLASS_MODE_STANDARD);
    ui_glass_component_t component = { 0 };
    component.root = plain_object(parent, x, y, width, height);
    component.label = label_create(component.root, label,
                                   &lv_font_montserrat_14, theme->text);
    lv_obj_set_pos(component.label, UI_GLASS_SPACE_MD, 3);
    lv_obj_set_width(component.label, width - UI_GLASS_SPACE_MD * 2 - 50);
    lv_label_set_long_mode(component.label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(component.label, lv_font_montserrat_14.line_height);
    component.value = label_create(component.root, "",
                                    &lv_font_montserrat_14, theme->text_muted);
    lv_obj_set_width(component.value, 44);
    lv_obj_set_style_text_align(component.value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(component.value, LV_ALIGN_TOP_RIGHT, -UI_GLASS_SPACE_MD, 3);
    // A long label and a horizontal track do not share one readable baseline
    // on the 196 px content width. Use the same two-line hierarchy as Progress:
    // label first, full-width track below, with a deliberate vertical gap.
    component.indicator = plain_object(
        component.root, UI_GLASS_SPACE_MD, 29,
        width - UI_GLASS_SPACE_MD * 2, 6);
    lv_obj_set_style_radius(component.indicator, LV_RADIUS_CIRCLE, 0);
    // The glass thumb is taller than the track. Keep it as a child so its
    // percentage is track-relative, but allow its optical rim to draw outside
    // the six-pixel track instead of clipping it into a progress dash.
    lv_obj_add_flag(component.indicator, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_color(component.indicator,
                              lv_color_hex(theme->text_muted), 0);
    lv_obj_set_style_bg_opa(component.indicator, LV_OPA_30, 0);
    component.auxiliary = ui_glass_surface_create(
        component.indicator, 0, -4, 14, 14, LV_RADIUS_CIRCLE,
        theme->accent, LV_OPA_90, UI_GLASS_MATERIAL_CLEAR);
    ui_glass_surface_set_edge_strength(component.auxiliary, 124);
    // ui_glass_slider_set() derives the thumb position from the track's
    // measured width, and LVGL only applies the size set above on its next
    // layout pass. Without this the create-time call measures zero and pins
    // the thumb to the left end, so the slider opened at 0 % whatever value
    // it was given. Later calls from a key press ran after a layout and did
    // move it, which is why only the initial position looked wrong.
    lv_obj_update_layout(component.indicator);
    ui_glass_slider_set(&component, percent, theme);
    return component;
}

void ui_glass_component_set_state(ui_glass_component_t *component,
                                  ui_glass_component_state_t state,
                                  const ui_glass_theme_t *theme)
{
    if (!component || !component->root || !theme) return;
    uint32_t color = theme->text;
    lv_opa_t opacity = LV_OPA_COVER;
    int transform = 0;

    switch (state) {
    case UI_GLASS_COMPONENT_FOCUSED:
    case UI_GLASS_COMPONENT_SELECTED:
        color = theme->accent;
        break;
    case UI_GLASS_COMPONENT_PRESSED:
        color = theme->accent;
        transform = -2;
        break;
    case UI_GLASS_COMPONENT_DISABLED:
        opacity = LV_OPA_40;
        break;
    case UI_GLASS_COMPONENT_LOADING:
        color = theme->warning;
        break;
    case UI_GLASS_COMPONENT_ERROR:
        color = theme->danger;
        break;
    case UI_GLASS_COMPONENT_DEFAULT:
    default:
        break;
    }

    lv_obj_set_style_transform_width(component->root, transform, 0);
    lv_obj_set_style_transform_height(component->root, transform, 0);
    lv_obj_set_style_opa(component->root, opacity, 0);
    if (component->label) {
        lv_obj_set_style_text_color(component->label, lv_color_hex(color), 0);
    }
}

void ui_glass_toggle_set(ui_glass_component_t *component, bool enabled,
                         const ui_glass_theme_t *theme)
{
    if (!component || !component->indicator || !component->auxiliary ||
        !theme) {
        return;
    }
    lv_obj_set_style_bg_color(
        component->indicator,
        lv_color_hex(enabled ? theme->positive : theme->text_muted), 0);
    lv_obj_set_style_bg_opa(component->indicator,
                            enabled ? LV_OPA_80 : LV_OPA_30, 0);
    lv_obj_set_style_bg_color(component->auxiliary,
                              lv_color_hex(theme->text), 0);
    lv_obj_set_style_bg_opa(component->auxiliary, LV_OPA_COVER, 0);
    lv_obj_set_x(component->auxiliary, enabled ? 20 : 2);
}

void ui_glass_slider_set(ui_glass_component_t *component, uint8_t percent,
                         const ui_glass_theme_t *theme)
{
    if (!component || !component->indicator || !component->auxiliary ||
        !theme) {
        return;
    }
    if (percent > 100) percent = 100;
    if (component->value) {
        lv_label_set_text_fmt(component->value, "%u%%", percent);
    }
    int track_width = lv_obj_get_width(component->indicator);
    int x = (track_width - 14) * percent / 100;
    lv_obj_set_x(component->auxiliary, x);
    lv_obj_set_style_bg_color(component->auxiliary,
                              lv_color_hex(theme->accent), 0);
    lv_obj_set_style_bg_opa(component->auxiliary, LV_OPA_COVER, 0);
}

void ui_glass_toggle_set_animated(ui_glass_component_t *component,
                                  bool enabled,
                                  const ui_glass_theme_t *theme,
                                  uint16_t duration_ms)
{
    if (!component || !component->indicator || !component->auxiliary ||
        !theme) {
        return;
    }
    lv_obj_set_style_bg_color(
        component->indicator,
        lv_color_hex(enabled ? theme->positive : theme->text_muted), 0);
    lv_obj_set_style_bg_opa(component->indicator,
                            enabled ? LV_OPA_80 : LV_OPA_30, 0);
    lv_obj_set_style_bg_color(component->auxiliary,
                              lv_color_hex(theme->text), 0);
    lv_obj_set_style_bg_opa(component->auxiliary, LV_OPA_COVER, 0);
    animate_thumb(component->auxiliary, enabled ? 20 : 2, duration_ms);
}

void ui_glass_slider_set_animated(ui_glass_component_t *component,
                                  uint8_t percent,
                                  const ui_glass_theme_t *theme,
                                  uint16_t duration_ms)
{
    if (!component || !component->indicator || !component->auxiliary ||
        !theme) {
        return;
    }
    if (percent > 100) percent = 100;
    if (component->value) {
        lv_label_set_text_fmt(component->value, "%u%%", percent);
    }
    int track_width = lv_obj_get_width(component->indicator);
    int target_x = (track_width - 14) * percent / 100;
    lv_obj_set_style_bg_color(component->auxiliary,
                              lv_color_hex(theme->accent), 0);
    lv_obj_set_style_bg_opa(component->auxiliary, LV_OPA_COVER, 0);
    animate_thumb(component->auxiliary, target_x, duration_ms);
}
