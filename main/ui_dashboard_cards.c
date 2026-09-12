#include "ui_dashboard_cards.h"

uint8_t ui_dashboard_card_next(uint8_t index, uint8_t count, int8_t direction)
{
    if (!count) return 0;
    index %= count;
    if (!direction) return index;
    return direction < 0 ? (uint8_t)((index + count - 1u) % count)
                         : (uint8_t)((index + 1u) % count);
}

bool ui_dashboard_card_tick(uint32_t delta_ms, bool paused,
                            uint32_t period_ms, uint32_t *elapsed_ms,
                            uint32_t *hold_ms)
{
    if (paused || !period_ms) return false;
    if (*hold_ms >= delta_ms) {
        *hold_ms -= delta_ms;
        return false;
    }
    delta_ms -= *hold_ms;
    *hold_ms = 0;
    if (delta_ms >= period_ms || *elapsed_ms >= period_ms - delta_ms) {
        *elapsed_ms = 0;
        return true;
    }
    *elapsed_ms += delta_ms;
    return false;
}
