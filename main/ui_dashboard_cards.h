#pragma once

#include <stdbool.h>
#include <stdint.h>

uint8_t ui_dashboard_card_next(uint8_t index, uint8_t count, int8_t direction);
// Greeting for the Home card. `hour` is local time 0-23. Pass synced=false
// before the first clock synchronization: the caller has no idea what time of
// day it is, and guessing one reads as a bug next to a --:-- clock.
const char *ui_dashboard_greeting(int hour, bool synced);
// Advance the automatic rotation clock. Pausing preserves the remaining hold.
bool ui_dashboard_card_tick(uint32_t delta_ms, bool paused,
                            uint32_t period_ms, uint32_t *elapsed_ms,
                            uint32_t *hold_ms);
