#pragma once

#include <stdbool.h>
#include <stdint.h>

uint8_t ui_dashboard_card_next(uint8_t index, uint8_t count, int8_t direction);
// Advance the automatic rotation clock. Pausing preserves the remaining hold.
bool ui_dashboard_card_tick(uint32_t delta_ms, bool paused,
                            uint32_t period_ms, uint32_t *elapsed_ms,
                            uint32_t *hold_ms);
