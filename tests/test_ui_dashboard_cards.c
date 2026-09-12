#include "ui_dashboard_cards.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(ui_dashboard_card_next(0, 3, -1) == 2);
    assert(ui_dashboard_card_next(2, 3, 1) == 0);
    assert(ui_dashboard_card_next(1, 3, -1) == 0);
    assert(ui_dashboard_card_next(1, 3, 0) == 1);
    assert(ui_dashboard_card_next(0, 0, 1) == 0);
    uint32_t elapsed = 7800, hold = 10000;
    assert(!ui_dashboard_card_tick(60000, true, 8000, &elapsed, &hold));
    assert(elapsed == 7800 && hold == 10000);
    assert(!ui_dashboard_card_tick(10000, false, 8000, &elapsed, &hold));
    assert(hold == 0 && elapsed == 7800);
    assert(ui_dashboard_card_tick(200, false, 8000, &elapsed, &hold));
    assert(elapsed == 0);
    hold = 100;
    assert(!ui_dashboard_card_tick(200, false, 8000, &elapsed, &hold));
    assert(elapsed == 100 && hold == 0);
    assert(!ui_dashboard_card_tick(200, false, 0, &elapsed, &hold));
    puts("Dashboard card navigation and timing: PASS");
    return 0;
}
