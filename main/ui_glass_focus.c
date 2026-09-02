#include "ui_glass_focus.h"

void ui_glass_focus_init(ui_glass_focus_model_t *model, uint8_t count,
                         uint8_t initial_index, bool wraps)
{
    if (!model) return;
    model->count = count;
    model->index = count == 0 ? 0 : initial_index % count;
    model->wraps = wraps;
}

bool ui_glass_focus_move(ui_glass_focus_model_t *model, int8_t delta)
{
    if (!model || model->count == 0 || delta == 0) return false;
    int16_t next = (int16_t)model->index + delta;
    if (model->wraps) {
        next %= model->count;
        if (next < 0) next += model->count;
    } else {
        if (next < 0) next = 0;
        if (next >= model->count) next = model->count - 1;
    }
    if ((uint8_t)next == model->index) return false;
    model->index = (uint8_t)next;
    return true;
}

void ui_glass_focus_set_count(ui_glass_focus_model_t *model, uint8_t count)
{
    if (!model) return;
    model->count = count;
    if (count == 0) model->index = 0;
    else if (model->index >= count) model->index = count - 1;
}
