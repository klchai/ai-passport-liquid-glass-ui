#include "ui_dashboard_cards.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_greeting(void)
{
    // 未同步时不猜时段：与时钟显示 --:-- 同一条原则。
    assert(strcmp(ui_dashboard_greeting(9, false), "Hello") == 0);
    assert(strcmp(ui_dashboard_greeting(0, false), "Hello") == 0);

    // 三个时段的边界。凌晨算 evening，所以 04:59 与 23:00 落在同一档。
    assert(strcmp(ui_dashboard_greeting(0, true), "Good evening") == 0);
    assert(strcmp(ui_dashboard_greeting(4, true), "Good evening") == 0);
    assert(strcmp(ui_dashboard_greeting(5, true), "Good morning") == 0);
    assert(strcmp(ui_dashboard_greeting(11, true), "Good morning") == 0);
    assert(strcmp(ui_dashboard_greeting(12, true), "Good afternoon") == 0);
    assert(strcmp(ui_dashboard_greeting(17, true), "Good afternoon") == 0);
    assert(strcmp(ui_dashboard_greeting(18, true), "Good evening") == 0);
    assert(strcmp(ui_dashboard_greeting(23, true), "Good evening") == 0);

    // 越界小时数按未知处理，不要越界读表。
    assert(strcmp(ui_dashboard_greeting(-1, true), "Hello") == 0);
    assert(strcmp(ui_dashboard_greeting(24, true), "Hello") == 0);
}

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
    test_greeting();
    puts("Dashboard card navigation, timing and greeting: PASS");
    return 0;
}
