#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t count;
    uint8_t index;
    bool wraps;
} ui_glass_focus_model_t;

void ui_glass_focus_init(ui_glass_focus_model_t *model, uint8_t count,
                         uint8_t initial_index, bool wraps);

// Moves by a signed number of items. Returns true only when the index changes.
bool ui_glass_focus_move(ui_glass_focus_model_t *model, int8_t delta);

// Updates the available item count while keeping the current index valid.
void ui_glass_focus_set_count(ui_glass_focus_model_t *model, uint8_t count);
